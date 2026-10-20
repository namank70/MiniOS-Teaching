# Deterministic suggestion policy

The host-side policy separates three concerns:

1. `minios_mock_predict()` maps validated context and model-view metadata to a
   stable suggestion ID.
2. `minios_policy_evaluate()` decides whether a time/location trigger makes
   evaluation eligible, applies the 30-minute idle gate and recent-ID
   suppression, and returns a semantic event record.
3. The caller publishes the returned event to its event bus and calls
   `minios_policy_commit()` only after the bus accepts it.

The policy triggers once for initial context, when a new context changes zone,
or when the absolute half-hour slot advances. It uses the scenario's explicit
start minute and logical `now_ms`; it never reads wall-clock time. A pending
event is returned unchanged if evaluation is retried before commit. Recent
history is updated only when the caller commits the accepted event.

Build and run this host unit test in Linux or WSL:

```sh
make test-suggestion-policy
```

These tests exercise portable host policy code only. They do not yet implement
the JSON scenario CLI, event bus, worker processes, kernel task integration, or
MiniOS shell command. The suggestion policy is illustrative teaching behavior,
not an automotive recommendation system.
