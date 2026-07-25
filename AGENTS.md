# Repository Guidelines

## Project Structure

SatCompute is an ns-3.33-based dynamic satellite-network simulator. The main
application is `examples/satcompute/satcompute.cc`. Topology construction and
runtime updates live in `examples/satcompute/topo.cc` and
`examples/satcompute/jsontopo/`; metrics live in
`examples/satcompute/metrics/`; experiment defaults live in
`examples/satcompute/para.cc`. Input examples are under
`examples/satcompute/input/`. Custom ns-3 clustering code remains a separate
module under `src/cluster/`.

## Build and Run

Use a project-local Python environment when available:

```bash
source .venv/bin/activate
./waf configure --enable-examples --enable-tests
./waf build
./waf --run "satcompute --routingMode=0 --offeredload=0 --simulationDuration=10 --nodesJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json --topologyJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/topology_0s.json --trafficMatrix=examples/satcompute/input/traffic/traffic_matrix(73).csv"
```

The smoke test should initialize 73 satellites and 6 ground stations, then
apply topology updates at 5 and 10 seconds.

## Code Conventions

Follow the existing ns-3 C++ style: GNU braces, two-space indentation, no tabs,
and `.cc`/`.h` filenames. Preserve GPL headers in ns-3 source files. Keep
JsonTopo snapshots named `nodes_<time>s.json`, `topology_<time>s.json`, or
`patch_<time>s.json`. Update user-facing paths and commands whenever the
application entry point or input layout changes.

## Verification

After simulation or topology changes, run `./waf build` and the customer-scale
smoke test above. Use `./test.py -s <suite>` or
`./waf --run "test-runner --suite=<suite>"` for affected ns-3 modules.

## Data and Generated Files

Do not commit `build/`, caches, packet captures, routing-table dumps, metrics,
or any `output/` directory. The legacy 324-satellite traffic matrix is kept
outside this repository because it exceeds GitHub's file-size limit; pass its
path with `--trafficMatrix` when needed. Keep small, intentional examples under
`examples/satcompute/input/`.
