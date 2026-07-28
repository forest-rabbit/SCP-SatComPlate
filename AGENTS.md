# Repository Guidelines

## Scope

SatCompute models satellite nodes and inter-satellite links only. Do not add
ground stations, feeder links, clusters, CSV topology construction, custom
intra/inter-cluster routing, SDN routing, or OpenFlow experiments.

## Project Structure

The main program is `contrib/satcompute/satcompute.cc`; active default
parameters are in `contrib/satcompute/para.cc`. Topology orchestration is in
`contrib/satcompute/topology/satellite-topology.cc`; snapshot types, JSON
parsing, and scheduling are in `contrib/satcompute/topology/snapshot/`;
runtime ISL state is in `contrib/satcompute/topology/link/`; metrics are in
`contrib/satcompute/metrics/`. The committed input is a small, pure-satellite
example under `contrib/satcompute/input/`.

## Build and Verify

```bash
source .venv/bin/activate
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
./waf --run-no-build "satcompute --simulationDuration=110 --outputDir=/tmp/satcompute-smoke"
```

The smoke test must create 66 satellites, load only ISLs, apply all static
10-second snapshots through 110 seconds, and recompute routes with stock
`Ipv4GlobalRouting`.

SatCompute end-to-end checks use JSON fixtures and the external Python checker;
they do not require `--enable-tests`. Upstream ns-3 test suites are optional
dependency checks and must be enabled explicitly when needed.

## Conventions

Follow ns-3 GNU C++ style: two-space indentation, GNU braces, no tabs, and
`.cc`/`.h` filenames. Preserve upstream GPL headers. Snapshot filenames must
use paired `nodes_<time>s.json` and `topology_<time>s.json` files; every
snapshot is full and must list the same satellite IDs.

## Generated Data

Do not commit `build/`, caches, packet captures, routing dumps, metrics, or any
`output/` directory. Keep sample topology and traffic files intentionally
small.
