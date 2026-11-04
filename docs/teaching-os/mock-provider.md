# Deterministic mock provider

The first Person B implementation is a host-testable C11 mock provider:

- `include/minios/mock_provider.h` defines the versioned input context, local
  model view, stable suggestion IDs, and result status.
- `src/shared/mock_provider.c` validates context/model metadata, reads the
  bounded model view to calculate a deterministic checksum, then applies the
  classroom rules in the documented precedence order.
- `tests/unit/mock_provider_test.c` covers repeated-input determinism,
  threshold behavior, rule outcomes, invalid inputs, and truncated model views.

Run the tests in the Linux/WSL environment with:

```sh
make test-mock-provider
```

The matrix is a deterministic memory workload, not a trained AI model. The
suggestion rules are a teaching mock and do not represent a real driving
recommendation system. This unit test does not exercise worker processes,
shared-memory attachment, the MiniOS kernel, or a shell. The model view is a
process-local view supplied by the region/platform layer; its `byte_length`
must describe the actual readable mapping.
