# Repository Guidelines

## Scope

SatCompute models satellite nodes and inter-satellite links only. Do not add
ground stations, feeder links, clusters, CSV topology construction, custom
intra/inter-cluster routing, SDN routing, or OpenFlow experiments.

## Project Structure

The main program is `examples/satcompute/satcompute.cc`. Topology orchestration
is in `examples/satcompute/topo.cc`; JSON parsing and runtime link state are in
`examples/satcompute/jsontopo/`; metrics are in
`examples/satcompute/metrics/`. The committed input is a small, pure-satellite
example under `examples/satcompute/input/`.

## Build and Verify

```bash
source .venv/bin/activate
./waf configure --enable-examples --enable-tests
./waf build
./waf --run "satcompute --simulationDuration=120 --offeredLoad=0 --outputDir=/tmp/satcompute-smoke"
./test.py -s devices-point-to-point
```

The smoke test must create 24 satellites, load only ISLs, apply the
60-second and 120-second snapshots, and recompute routes with stock
`Ipv4GlobalRouting`.

## Conventions

Follow ns-3 GNU C++ style: two-space indentation, GNU braces, no tabs, and
`.cc`/`.h` filenames. Preserve upstream GPL headers. Snapshot filenames must
use `YYYY-MM-DD_HH-MM-SS.json`; every snapshot is a full ISL snapshot and must
list the same satellite IDs.

## Generated Data

Do not commit `build/`, caches, packet captures, routing dumps, metrics, or any
`output/` directory. Keep sample topology and traffic files intentionally
small.
