# Topology replay snapshots

The replay layer accepts both legacy SatCompute pairs and the self-describing
0.2 pairs emitted by `topology/export/`.

For a version 0.2 directory, `manifest.json` is authoritative. Replay validates
its closed-world root and ordered slice records, verifies every listed node and
topology SHA-256, ignores files not listed in the manifest, and then
down-selects the requested network cadence. This permits safe reuse of a fine
1 s or 2 s trace at a 20 s network cadence and prevents a stale or modified
file from silently entering a run. The generating scenario hash is retained as
provenance; it is not required to equal the replay scenario hash because the
replay scenario intentionally changes the topology source and may select a
different cadence.

Each 0.2 pair contains the same `simulation_time_ns` and
`state_semantics=orbit-policy-evaluation`. Node slices contain stable IDs and
ECEF x/y/z coordinates in metres. Topology slices contain active canonical
plus-grid links, current distance in metres, one-way delay in integer
nanoseconds, and bandwidth in bits per second. The reader checks the embedded
time against the exact filename time selected by the schedule. The complete
contracts are `topology/export/nodes-slice.schema.json`,
`topology/export/topology-slice.schema.json`, and
`topology/export/manifest.schema.json`.

Legacy directories have no manifest and retain the original pair contract:

- `nodes_<seconds>s.json` contains exactly a `nodes` array. Each item contains
  exactly `node_id` and `node_type`; the type must be `sat`.
- `topology_<seconds>s.json` contains exactly a `links` array. Each item
  contains `node1_id`, `node2_id`, `type`, `delay`, and `link_bandwidth`.
Legacy `delay` is in microseconds and `link_bandwidth` is in kilobits per
second. The loader converts them to integer nanoseconds and bits per second
once. A pair cannot mix legacy and 0.2 encodings.

Timestamp tokens are converted exactly to integer nanoseconds. Aliases such as
`1s` and `1.0s` are therefore duplicate timestamps, and precision finer than a
nanosecond is rejected.

The directory may contain snapshots more frequently than the current scenario
needs. Replay selects only `0`, `network_update_interval_s`, twice that interval,
and so on strictly before `simulation.duration_s`. A final exported state
exactly at the duration is therefore audit output and is not applied as a live
network update. Every selected timestamp must have a complete nodes/topology
pair; unselected complete pairs remain available for other scenarios or future
fault generation but are not applied.

An empty `links` array is valid because a distance gate or future fault overlay
may temporarily remove every active ISL. The satellite ID set must be non-empty,
unique, and include every link endpoint.

For both encodings, scenario bandwidth remains authoritative at replay time.
In fixed mode the scenario fixed delay overrides every stored link delay. In
distance mode replay consumes the legacy microsecond delay or the 0.2 integer
nanosecond delay exactly once. Coordinates are retained by the 0.2 reader for
audit/equivalence consumers; the current replay network controller does not
recompute the already-recorded topology from them.
