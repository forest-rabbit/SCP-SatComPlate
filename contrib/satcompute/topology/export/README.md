# Online orbit state and topology trace export

SatCompute 0.2 can evaluate the same ns-3.48 circular-orbit and fixed-candidate
topology policy used by the live simulator and emit deterministic JSON slices.
The output is a read-only state boundary for audit, replay, future fault-model
generation, and a later backend/frontend adapter. This module does not open a
socket or define a frontend transport protocol.

The three closed-world output contracts are:

- `nodes-slice.schema.json` for stable satellite IDs and ECEF x/y/z positions;
- `topology-slice.schema.json` for active fixed-candidate ISLs, distance,
  propagation delay, and bandwidth;
- `manifest.schema.json` for provenance, cadence, hashes, and the authoritative
  ordered file inventory.

All human-facing cadence values remain seconds in the scenario. The loader
converts them exactly once to signed 64-bit integer nanoseconds. A trace always
contains time zero, every positive trace interval strictly before the
simulation duration, and—when `include_final_state` is true—one exact final
state without duplication. Filename tokens are canonical decimal seconds, for
example `nodes_0s.json`, `nodes_1s.json`, and `nodes_2.5s.json`.

## Trace cadence versus network cadence

`trace_export.interval_s` and `network.network_update_interval_s` are
independent inputs. Every exported slice has
`state_semantics=orbit-policy-evaluation`: it evaluates continuous orbital
positions, the fixed plus-grid candidates, the distance gate, and link delay at
that exact trace time. It does not claim that the simulated network applied an
interface or routing update at that time.

For example, a scenario may export every 1 s while updating the simulated
network every 20 s. The 1–19 s files are fine-grained policy states for audit or
fault generation. The live network retains its state from the 0 s update until
the 20 s update. At shared timestamps such as 0 s and 20 s, both paths evaluate
the same orbit object and topology policy and must agree.

## Invocation

Normal online execution can emit the trace alongside network, routing, and
workload outputs:

```bash
./ns3 run "satcompute \
  --scenarioConfig=contrib/satcompute/tests/fixtures/scenario/online-trace.json \
  --outputDir=/tmp/satcompute-online-trace"
```

The same scenario can generate only orbit/topology state without installing
network devices, routes, or workloads:

```bash
./ns3 run "satcompute \
  --scenarioConfig=contrib/satcompute/tests/fixtures/scenario/online-trace.json \
  --outputDir=/tmp/satcompute-export-only \
  --exportOnly=true"
```

`--exportOnly=true` requires an online `ns3-circular` scenario with trace
export enabled and is mutually exclusive with `--validateOnly=true`. A normal
run and export-only run using identical scenario bytes produce byte-identical
`topology-trace/` contents.

Consumers must start from `topology-trace/manifest.json`, verify each recorded
SHA-256, and process only listed files. The manifest is authoritative if an
output directory contains stale files from an older run. Use a fresh output
directory for operational runs whenever possible.

Coordinates are ECEF metres. Active link distance is in metres, delay is
integer nanoseconds, and bandwidth is bits per second. Fixed delay uses the
scenario value. Distance delay divides raw ECEF separation by 299792458 m/s and
rounds to the nearest nanosecond with exact halves upward, exactly as the live
online controller does.
