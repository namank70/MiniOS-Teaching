# Host scenario runner

`tools/minios_scenario.py` reads a versioned JSON fixture and calls the same C
mock provider and suggestion-policy functions used by the unit tests. A small C
adapter exposes a primitive, process-local interface to Python; it owns a
deterministically initialized 256x256 integer mock matrix for each run.

Run a fixture from Linux or WSL:

```sh
make scenario SCENARIO=tests/scenarios/refuel.json
```

Run the scenario-runner validation tests:

```sh
make test-scenarios
```

Run the full current Person B host test set:

```sh
make test-person-b
```

The fixture contains `format_version`, `start_minute_of_day`, `model_version`,
an `initial_context`, ordered `events`, and exact `expected_events`. Context
events use logical milliseconds, increasing input sequence numbers, one of
`ROAD`, `STATION`, `REST_STOP`, or `DESTINATION`, a fuel percentage, and idle
minutes. Time is derived only from the fixture; the runner does not read the
wall clock.

For each emitted semantic event the runner writes one compact JSON object to
stdout with fixed field order. Setup diagnostics go to stderr. Exit codes:

- `0`: actual semantic event trace exactly matches the fixture.
- `1`: valid fixture, but its expected trace differs from actual behavior.
- `2`: malformed fixture or runner setup error.

The test suite checks successful exact matching, repeatability, malformed
fixture handling, and expected-output mismatch handling. These are host tests:
they do not exercise POSIX workers, shared-memory attachment, kernel scheduling,
QEMU, or a MiniOS shell command. The Python file parses the fixture; policy
decisions remain in the shared C implementation.
