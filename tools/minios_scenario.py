#!/usr/bin/env python3
"""Run a deterministic MiniOS context fixture against the portable C policy."""

import argparse
import ctypes
import json
import sys
from pathlib import Path


FORMAT_VERSION = 1
MODEL_VERSION = 1
POLICY_EVENT_READY = 4
POLICY_NO_TRIGGER = 0
POLICY_SUPPRESSED_IDLE = 1
POLICY_NO_SUGGESTION = 2
POLICY_SUPPRESSED_REPEAT = 3

ZONES = {
    "ROAD": 0,
    "STATION": 1,
    "REST_STOP": 2,
    "DESTINATION": 3,
}
SUGGESTIONS = {
    1: "REFUEL",
    2: "TAKE_BREAK",
    3: "PREPARE_ARRIVAL",
}
NO_EVENT_STATUSES = {
    POLICY_NO_TRIGGER,
    POLICY_SUPPRESSED_IDLE,
    POLICY_NO_SUGGESTION,
    POLICY_SUPPRESSED_REPEAT,
}


class FixtureError(Exception):
    """The fixture is malformed or violates its input contract."""


def require_object(value, name, required_keys):
    if not isinstance(value, dict):
        raise FixtureError(f"{name} must be a JSON object")
    missing = required_keys - value.keys()
    extra = value.keys() - required_keys
    if missing:
        raise FixtureError(f"{name} missing fields: {', '.join(sorted(missing))}")
    if extra:
        raise FixtureError(f"{name} has unknown fields: {', '.join(sorted(extra))}")
    return value


def require_uint(value, name, maximum):
    if isinstance(value, bool) or not isinstance(value, int):
        raise FixtureError(f"{name} must be an integer")
    if value < 0 or value > maximum:
        raise FixtureError(f"{name} must be in range 0..{maximum}")
    return value


def validate_context(context, name, sequence_required=True):
    keys = {"zone", "fuel_percent", "idle_minutes"}
    if sequence_required:
        keys.add("seq")
    require_object(context, name, keys)
    zone = context["zone"]
    if not isinstance(zone, str) or zone not in ZONES:
        raise FixtureError(f"{name}.zone must be one of {', '.join(ZONES)}")
    fuel = require_uint(context["fuel_percent"], f"{name}.fuel_percent", 100)
    idle = require_uint(context["idle_minutes"], f"{name}.idle_minutes", 0xFFFFFFFF)
    sequence = None
    if sequence_required:
        sequence = require_uint(context["seq"], f"{name}.seq", 0xFFFFFFFFFFFFFFFF)
    return ZONES[zone], fuel, idle, sequence


def load_fixture(path):
    try:
        fixture = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FixtureError(f"cannot read valid JSON fixture: {error}") from error

    required = {
        "format_version",
        "start_minute_of_day",
        "model_version",
        "initial_context",
        "events",
        "expected_events",
    }
    require_object(fixture, "fixture", required)

    version = require_uint(fixture["format_version"], "format_version", 0xFFFFFFFF)
    if version != FORMAT_VERSION:
        raise FixtureError(f"unsupported format_version {version}")

    start_minute = require_uint(
        fixture["start_minute_of_day"], "start_minute_of_day", 1439
    )
    model_version = require_uint(fixture["model_version"], "model_version", 0xFFFFFFFF)
    if model_version != MODEL_VERSION:
        raise FixtureError(f"unsupported model_version {model_version}")

    initial = fixture["initial_context"]
    initial_zone, initial_fuel, initial_idle, initial_sequence = validate_context(
        initial, "initial_context"
    )

    events = fixture["events"]
    if not isinstance(events, list):
        raise FixtureError("events must be a JSON array")

    validated_events = []
    previous_time = 0
    previous_sequence = initial_sequence
    for index, event in enumerate(events):
        name = f"events[{index}]"
        require_object(event, name, {"at_ms", "seq", "op", "zone",
                                     "fuel_percent", "idle_minutes"})
        at_ms = require_uint(event["at_ms"], f"{name}.at_ms", 0xFFFFFFFFFFFFFFFF)
        sequence = require_uint(event["seq"], f"{name}.seq", 0xFFFFFFFFFFFFFFFF)
        if at_ms < previous_time:
            raise FixtureError(f"{name}.at_ms moves backwards")
        if sequence < previous_sequence:
            raise FixtureError(f"{name}.seq moves backwards")
        if event["op"] != "context":
            raise FixtureError(f"{name}.op must be 'context'")
        context_fields = {
            "zone": event["zone"],
            "fuel_percent": event["fuel_percent"],
            "idle_minutes": event["idle_minutes"],
            "seq": event["seq"],
        }
        zone, fuel, idle, _ = validate_context(context_fields, name)
        validated_events.append((at_ms, sequence, zone, fuel, idle))
        previous_time = at_ms
        previous_sequence = sequence

    expected = fixture["expected_events"]
    if not isinstance(expected, list):
        raise FixtureError("expected_events must be a JSON array")
    expected_keys = {
        "type", "actor", "at_ms", "seq", "model_version", "suggestion"
    }
    for index, event in enumerate(expected):
        require_object(event, f"expected_events[{index}]", expected_keys)
        if event["type"] != "SUGGESTION":
            raise FixtureError(f"expected_events[{index}].type must be SUGGESTION")
        if event["suggestion"] not in SUGGESTIONS.values():
            raise FixtureError(
                f"expected_events[{index}].suggestion is not a known suggestion ID"
            )
        require_uint(event["actor"], f"expected_events[{index}].actor", 0xFFFFFFFF)
        require_uint(event["at_ms"], f"expected_events[{index}].at_ms", 0xFFFFFFFFFFFFFFFF)
        require_uint(event["seq"], f"expected_events[{index}].seq", 0xFFFFFFFFFFFFFFFF)
        require_uint(
            event["model_version"],
            f"expected_events[{index}].model_version",
            0xFFFFFFFF,
        )

    return (
        start_minute,
        (initial_zone, initial_fuel, initial_idle, initial_sequence),
        validated_events,
        expected,
    )


def load_policy_library(path):
    try:
        library = ctypes.CDLL(str(path))
    except OSError as error:
        raise FixtureError(f"cannot load policy adapter: {error}") from error

    library.minios_scenario_init.argtypes = [ctypes.c_uint32]
    library.minios_scenario_init.restype = ctypes.c_int
    library.minios_scenario_evaluate.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint64),
        ctypes.POINTER(ctypes.c_uint64),
    ]
    library.minios_scenario_evaluate.restype = ctypes.c_int
    library.minios_scenario_commit.argtypes = [ctypes.c_uint64]
    library.minios_scenario_commit.restype = ctypes.c_int
    return library


def evaluate(library, start_minute, event):
    at_ms, sequence, zone, fuel, idle = event
    minute_of_day = (start_minute + at_ms // 60000) % 1440
    actor = ctypes.c_uint32()
    model_version = ctypes.c_uint32()
    suggestion = ctypes.c_uint32()
    event_time = ctypes.c_uint64()
    event_sequence = ctypes.c_uint64()

    status = library.minios_scenario_evaluate(
        at_ms,
        minute_of_day,
        zone,
        fuel,
        idle,
        sequence,
        ctypes.byref(actor),
        ctypes.byref(model_version),
        ctypes.byref(suggestion),
        ctypes.byref(event_time),
        ctypes.byref(event_sequence),
    )
    if status in NO_EVENT_STATUSES:
        return status, None
    if status != POLICY_EVENT_READY:
        raise FixtureError(f"policy rejected event with status {status}")
    if suggestion.value not in SUGGESTIONS:
        raise FixtureError(f"policy returned unknown suggestion ID {suggestion.value}")

    semantic_event = {
        "type": "SUGGESTION",
        "actor": actor.value,
        "at_ms": event_time.value,
        "seq": event_sequence.value,
        "model_version": model_version.value,
        "suggestion": SUGGESTIONS[suggestion.value],
    }
    commit_status = library.minios_scenario_commit(event_sequence.value)
    if commit_status != POLICY_EVENT_READY:
        raise FixtureError(f"policy event commit failed with status {commit_status}")
    return status, semantic_event


def run_fixture(library, fixture):
    start_minute, initial, events, expected = fixture
    init_status = library.minios_scenario_init(start_minute)
    if init_status != POLICY_NO_TRIGGER:
        raise FixtureError(f"policy initialization failed with status {init_status}")

    initial_event = (0, initial[3], initial[0], initial[1], initial[2])
    _, emitted = evaluate(library, start_minute, initial_event)
    trace = []
    if emitted is not None:
        trace.append(emitted)

    for event in events:
        _, emitted = evaluate(library, start_minute, event)
        if emitted is not None:
            trace.append(emitted)
    return trace, expected


def main():
    parser = argparse.ArgumentParser(
        description="Run a deterministic MiniOS scenario against the C policy."
    )
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--check", action="store_true", required=True)
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()

    try:
        fixture = load_fixture(args.fixture)
        library = load_policy_library(args.library)
        actual, expected = run_fixture(library, fixture)
    except FixtureError as error:
        print(f"scenario setup error: {error}", file=sys.stderr)
        return 2

    for event in actual:
        print(json.dumps(event, separators=(",", ":")))

    if actual != expected:
        print(
            f"scenario mismatch: expected {len(expected)} event(s), "
            f"got {len(actual)}",
            file=sys.stderr,
        )
        for index, (expected_event, actual_event) in enumerate(
            zip(expected, actual)
        ):
            if expected_event != actual_event:
                print(
                    f"first mismatch at event {index}: expected "
                    f"{expected_event!r}, got {actual_event!r}",
                    file=sys.stderr,
                )
                break
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
