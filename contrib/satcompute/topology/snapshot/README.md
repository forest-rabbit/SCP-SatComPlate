# Topology replay snapshots

The replay layer accepts the legacy SatCompute pair of files at each timestamp:

- `nodes_<seconds>s.json` contains exactly a `nodes` array. Each item contains
  exactly `node_id` and `node_type`; the type must be `sat`.
- `topology_<seconds>s.json` contains exactly a `links` array. Each item
  contains `node1_id`, `node2_id`, `type`, `delay`, and `link_bandwidth`.
  Legacy `delay` is in microseconds and `link_bandwidth` is in kilobits per
  second. The loader converts bandwidth to bits per second once.

Timestamp tokens are converted exactly to integer nanoseconds. Aliases such as
`1s` and `1.0s` are therefore duplicate timestamps, and precision finer than a
nanosecond is rejected.

The directory may contain snapshots more frequently than the current scenario
needs. Replay selects only `0`, `network_update_interval_s`, twice that interval,
and so on strictly before `simulation.duration_s`. Every selected timestamp
must have a complete nodes/topology pair; unselected complete pairs are retained
for other scenarios or future fault generation but are not applied.

An empty `links` array is valid because a distance gate or future fault overlay
may temporarily remove every active ISL. The satellite ID set must be non-empty,
unique, and include every link endpoint.
