#!/usr/bin/env python3
"""Boot the real GRUB ISO and exercise UART + emulated PS/2 input, not host stubs."""
import json
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import sys
import tempfile
import time

iso = Path(sys.argv[1]).resolve()
output = iso.parent
transcript = bytearray()
checks = []

with tempfile.TemporaryDirectory(prefix='minios-qemu-') as temp:
    serial_path = os.path.join(temp, 'serial.sock')
    qmp_path = os.path.join(temp, 'qmp.sock')
    with (output / 'qemu-stderr.log').open('wb') as err:
        proc = subprocess.Popen([
            os.environ.get('TEACHING_QEMU', 'qemu-system-i386'),
            '-accel', 'tcg', '-m', '64M', '-cdrom', str(iso), '-boot', 'd',
            '-display', 'none', '-no-reboot', '-no-shutdown',
            '-serial', f'unix:{serial_path},server=on,wait=on',
            '-qmp', f'unix:{qmp_path},server=on,wait=off',
        ], stdout=subprocess.DEVNULL, stderr=err)
        sockets = []
        try:
            def connect(path):
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sockets.append(sock)
                deadline = time.monotonic() + 20
                while True:
                    try:
                        sock.connect(path)
                        return sock
                    except (FileNotFoundError, ConnectionRefusedError):
                        if proc.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('QEMU did not expose its console')
                        time.sleep(0.02)

            serial = connect(serial_path)
            qmp = connect(qmp_path)
            qmp.settimeout(10)
            qfile = qmp.makefile('rwb', buffering=0)
            json.loads(qfile.readline())

            def qcommand(execute, arguments=None):
                qfile.write((json.dumps({'execute': execute, 'arguments': arguments or {}}) + '\n').encode())
                while True:
                    response = json.loads(qfile.readline())
                    if 'error' in response:
                        raise AssertionError(response)
                    if 'return' in response:
                        return response['return']

            qcommand('qmp_capabilities')
            cursor = 0

            def expect(needle, timeout=20):
                global cursor
                target = needle.encode()
                deadline = time.monotonic() + timeout
                while True:
                    index = transcript.find(target, cursor)
                    if index >= 0:
                        result = bytes(transcript[cursor:index + len(target)]).decode(errors='replace')
                        cursor = index + len(target)
                        return result
                    if b'PANIC' in transcript:
                        raise AssertionError(transcript.decode(errors='replace'))
                    if time.monotonic() > deadline:
                        raise AssertionError(f'Timed out waiting for {needle!r}\n' + transcript.decode(errors='replace'))
                    readable, _, _ = select.select([serial], [], [], 0.1)
                    if readable:
                        chunk = serial.recv(65536)
                        if not chunk:
                            raise RuntimeError('Console closed')
                        transcript.extend(chunk)

            def command(text, result):
                serial.sendall(text.encode())
                response = expect(result)
                response += expect('minios> ')
                return response

            expect('BOOT: MiniOS teaching i686', 40)
            expect('minios> ')
            checks.append('fresh ISO boot and shell prompt')
            command('help\r\n', 'progress Show shared progress snapshots')
            command('status\n', 'Console: COM1 + PS/2; provider: deterministic mock')
            checks.append('Naman help/status through kernel output callback')
            # Do not type suggest: require the scheduled task to publish on its own.
            expect('[proactive] REFUEL (scheduled mock context)', 20)
            expect('minios> ')
            checks.append('unsolicited scheduled recommendation')
            command('suggest\n', 'Proactive suggestion: REFUEL')
            response = command('test\n', 'PASS: kernel timer and sleep')
            assert 'PASS: kernel regions and policy' in response, response
            assert 'FAIL:' not in response, response
            checks.append('original dispatcher test plus actual shared model/policy test')
            first = command('tasks\n', '5 idle')
            # response already includes the prompt and all rows.
            task_pattern = r'(\d) (shell|recommendation|runtime|worker-a|worker-b|idle) (READY|RUNNING|SLEEPING) (\d) (\d+)'
            rows = re.findall(task_pattern, first)
            assert len(rows) == 6 and all(int(row[4]) > 0 for row in rows), first
            command('progress\n', 'ACTOR COMPLETED TOTAL HEARTBEAT_MS')
            command('unknown\n', "Unknown command. Type 'help'.")
            command('help argument\n', 'Usage: enter one command at a time')
            command('x' * 70 + '\n', 'Error: line too long (64 max); discarded')
            command('helx\x7fp\n', 'Commands:')
            serial.sendall(b'\n')
            expect('minios> ')
            command('x' * 64 + '\n', "Unknown command. Type 'help'.")
            checks.append('invalid, argument, overflow, backspace, empty and 64-byte boundary input')
            # A separate input path: QEMU synthesizes keyboard scan codes, not serial bytes.
            for key in ['h', 'e', 'l', 'p', 'ret']:
                qcommand('human-monitor-command', {'command-line': f'sendkey {key} 10'})
                time.sleep(0.04)
            expect('Commands:')
            expect('minios> ')
            checks.append('PS/2 keyboard command via QMP sendkey')

            # Independently calculate every partial result from the specified
            # model formula, not from output or a kernel-provided checksum.
            expected = []
            sums = [0, 0]
            for completed in range(1, 21):
                for worker in range(2):
                    row = worker * 20 + completed - 1
                    sums[worker] += sum(((17 * row + 31 * col) % 101) - 50 for col in range(256))
                    expected.append((worker + 5, worker + 3, completed, completed, 20,
                                     'DONE' if completed == 20 else 'RUNNING', sums[worker], 0))
            event_pattern = (r'\[work\] commit=(\d+) run=(\d+) actor=(\d+) task=(\d+) seq=(\d+) '
                             r'completed=(\d+)/(\d+) state=(\w+) result=(-?\d+) error=(\d+)')

            def verify_work(response, run):
                events = re.findall(event_pattern, response)
                assert len(events) == 40, response
                normalized = []
                for index, event in enumerate(events):
                    commit_id, run_id, actor, task, seq, done, total, state, result, error = event
                    assert int(commit_id) == (run - 1) * 40 + index + 1, event
                    assert int(run_id) == run, event
                    normalized.append((int(actor), int(task), int(seq), int(done), int(total),
                                       state, int(result), int(error)))
                assert normalized == expected, (normalized, expected)
                return normalized

            def finish_work(run):
                result = expect(f'WORK DONE run={run} events=40 result_a={sums[0]} result_b={sums[1]} PASS')
                return result + expect('minios> ')

            traces = []
            for run in (1, 2):
                result = command('work\n', f'WORK START run={run} observer=watching')
                result += finish_work(run)
                traces.append(verify_work(result, run))
            assert traces[0] == traces[1]
            checks.append('two scheduled workers: 40 ordered partial/final results, identical across two runs')

            command('work-pause\n', 'WORK START run=3 observer=paused')
            deadline = time.monotonic() + 20
            while True:
                paused = command('progress\n', 'observer=paused')
                full = re.search(r'pending=32/32 full_retries=([1-9]\d*)', paused)
                if full:
                    break
                assert time.monotonic() < deadline, paused
                time.sleep(0.05)
            for actor in (5, 6):
                assert re.search(rf'\[snapshot\] run=3 actor={actor} task=\d seq=16 completed=16/20 state=RUNNING', paused), paused
            command('work\n', 'Work start: BUSY')
            command('status\n', '; tasks=6; model_bytes=262144')
            result = command('updates\n', '[work] commit=112')
            result += command('watch\n', 'Work observer resumed.')
            result += finish_work(3)
            verify_work(result, 3)
            retained = command('progress\n', 'observer=watching')
            assert 'pending=0/32' in retained, retained
            for actor, total in zip((5, 6), sums):
                assert re.search(rf'\[snapshot\] run=3 actor={actor} task=\d seq=20 completed=20/20 state=DONE result={total}', retained), retained
            command('updates\n', 'No committed updates pending.')
            checks.append('paused observer fills history; shell stays live; BUSY prevents reset; retry loses no events/results')

            later = command('tasks\n', '5 idle')
            rows_later = re.findall(task_pattern, later)
            assert len(rows_later) == 6, later
            assert int(rows_later[0][4]) > int(rows[0][4]), (rows, rows_later)
            assert int(rows_later[2][4]) > int(rows[2][4]), (rows, rows_later)
            assert all(int(rows_later[i][4]) > int(rows[i][4]) for i in (3, 4)), (rows, rows_later)
            assert transcript.count(b'[proactive] REFUEL') == 1
            checks.append('continued task scheduling and duplicate suppression')
            assert b'FAIL' not in transcript and b'PANIC' not in transcript
            qcommand('quit')
            proc.wait(timeout=5)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            for sock in sockets:
                sock.close()
            (output / 'shell-smoke.log').write_bytes(transcript)

for check in checks:
    print('PASS:', check)
print('QEMU shell smoke passed; transcript:', output / 'shell-smoke.log')
