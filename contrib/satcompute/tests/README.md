# SatCompute tests

This directory contains only SatCompute-owned tests.  The ns-3 upstream test
layout remains unchanged.

- `unit/` contains the four Python unit-test domains.
- `integration/smoke/` contains fast simulation and tool contracts.
- `integration/regression/` contains the extended routing and workload
  regressions.
- `fixtures/` contains test-only JSON inputs.  Production examples and
  workloads remain under `../input/`.
- `support/` provides stable repository paths and shared fixture helpers.

From the repository root, run all Python unit tests with:

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
```

The integration scripts expect SatCompute to be configured and built first:

```bash
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh
contrib/satcompute/tests/integration/smoke/run-task-smoke.sh
contrib/satcompute/tests/integration/smoke/run-diagnostics-smoke.sh
contrib/satcompute/tests/integration/regression/run-full-routing-regression.sh
contrib/satcompute/tests/integration/regression/run-full-workload-regression.sh
```

Tool-specific smoke drivers remain separate because they use different
dependency groups:

```bash
uv run --locked python -m \
  contrib.satcompute.tests.integration.smoke.interval_analysis
MPLBACKEND=Agg uv run --locked --group visualization python -m \
  contrib.satcompute.tests.integration.smoke.orbit_visualization \
  --work-dir /tmp/satcompute-orbit-smoke
```

All CI, review, smoke, and regression outputs belong under `/tmp`.  Formal
experiment output may use `../output/<experiment>/`, but generated output
must never be committed.
