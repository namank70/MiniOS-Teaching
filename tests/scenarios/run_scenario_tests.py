#!/usr/bin/env python3
"""Verify scenario runner output and documented exit codes."""

import subprocess
import sys
from pathlib import Path


def run(runner, library, fixture):
    return subprocess.run(
        [
            sys.executable,
            str(runner),
            str(fixture),
            "--check",
            "--library",
            str(library),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main():
    runner = Path(sys.argv[1]).resolve()
    library = Path(sys.argv[2]).resolve()
    scenarios = Path(sys.argv[3]).resolve()

    refuel = run(runner, library, scenarios / "refuel.json")
    if refuel.returncode != 0:
        print(refuel.stderr, file=sys.stderr, end="")
        return 1
    expected_refuel = (
        '{"type":"SUGGESTION","actor":3,"at_ms":0,"seq":1,'
        '"model_version":1,"suggestion":"REFUEL"}\n'
    )
    if refuel.stdout != expected_refuel:
        print("refuel fixture did not produce the exact normalized event",
              file=sys.stderr)
        return 1

    repeated = run(runner, library, scenarios / "refuel.json")
    if repeated.returncode != 0 or repeated.stdout != refuel.stdout:
        print("repeated fixture run was not deterministic", file=sys.stderr)
        return 1

    rest_stop = run(runner, library, scenarios / "rest-stop-break.json")
    if rest_stop.returncode != 0 or '"suggestion":"TAKE_BREAK"' not in rest_stop.stdout:
        print("rest-stop boundary fixture did not pass", file=sys.stderr)
        return 1

    invalid = run(runner, library, scenarios / "invalid-time.json")
    if invalid.returncode != 2 or invalid.stdout:
        print("malformed fixture did not return setup-error status 2",
              file=sys.stderr)
        return 1

    mismatch = run(runner, library, scenarios / "expected-mismatch.json")
    if mismatch.returncode != 1 or '"suggestion":"REFUEL"' not in mismatch.stdout:
        print("deliberate expected-output mismatch did not return 1",
              file=sys.stderr)
        return 1

    print("PASS: scenario runner (5 checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
