# Performance

**Status: Phase 0 — no benchmarks exist.** No performance numbers will be published here until they have actually been measured (Phase 8). Correctness comes first; optimization is measured, not assumed.

## Planned methodology (Phase 8)

Once a working runtime exists, `syj-edgemind benchmark` will report, per model/context configuration, on real hardware:

- Startup time
- Model load time
- Memory usage (peak RSS)
- Prompt processing tokens/sec
- Generation tokens/sec
- Context behavior near the configured limit

Example planned output format:

```
SYJ EdgeMind Benchmark

Model: ...
Quantization: ...
Context: 1024

Model load:       ...
Prompt tokens:    ...
Generation:       ...
Tokens/sec:       ...
Peak memory:      ...
```

Every field will be a real measurement from a specific machine/config, documented alongside the number — not an estimate presented as a result.
