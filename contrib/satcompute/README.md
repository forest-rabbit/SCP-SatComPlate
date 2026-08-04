# SatCompute on ns-3.48

This contrib module is the main SCP-SatComPlate implementation. It is built as
a normal ns-3 library plus a module-owned `satcompute` executable, so neither
the global ns-3 examples switch nor the upstream test suite is required.

From the repository root:

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

The current scenario contract is `config/scenario.schema.json`. Its
`description` entries are the parameter reference; maintained configurations
are under `input/examples/`.

Direct network workloads use the closed-world
`traffic/transfer-trace.schema.json` contract. Its legacy-compatible nanosecond
arrival field and deterministic five-tuple/chunk derivation are documented in
`traffic/README.md`.

Compute resources and task workloads use the documented contracts under
`task/`. JSON replay and online circular-orbit scenarios use the same platform
entry point and workload engines. For example:

```bash
./ns3 run "satcompute \
  --scenarioConfig=contrib/satcompute/tests/fixtures/scenario/task-replay.json \
  --outputDir=/tmp/satcompute-run"
```

```bash
./ns3 run "satcompute \
  --scenarioConfig=contrib/satcompute/tests/fixtures/scenario/online-task.json \
  --outputDir=/tmp/satcompute-online-run"
```

Each run writes its effective configuration, transfer/task metrics, routing
metrics, run summary, and any partial-run diagnostics. Online positions come
directly from ns-3.48's circular-orbit mobility model; the configured network
interval samples those continuous positions without changing the orbit clock.

`workloads.task_completion_policy=strict` returns exit code 3 after recording
a partial run; `report` records the same diagnostics and returns success.
`--validateOnly=true` performs strict input resolution and hashing without
starting a simulation.

Execution applies `randomness.seed` and `randomness.run` before constructing
the topology or workloads. The current replay and online orbit cores are
otherwise deterministic and consume no random streams; `stream_start` is
reserved for future explicit failure-model stream assignment rather than
silently perturbing routing.

Legacy-compatible JSON replay slices and their exact integer-nanosecond
selection rules are documented under `topology/snapshot/`. A scenario may point
at a finer replay directory while its own `network_update_interval_s` controls
which complete pairs the simulator consumes.
