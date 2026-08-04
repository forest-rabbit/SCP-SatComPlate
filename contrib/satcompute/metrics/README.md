# Structured run outputs

`RunOutputWriter` persists only fields backed by an explicit SatCompute runtime
data source. It does not emit placeholder FlowMonitor or per-link queue metrics
before those collectors are connected.

Every run writes `run-summary.json` and `routing-summary.json`. A network
workload additionally writes `transfer-summary.csv` and
`udp-socket-drops.csv`; task mode adds `task-events.csv`, `task-summary.csv`,
and `compute-node-summary.csv`. Reservation-aware modes also write
`routing-reservation-events.csv`.

The run summary references the resolved `effective-config.json` by canonical
path and SHA-256. Counts and byte totals are cross-checked against the UDP
applications before publication. CSV rows follow canonical transfer/task
order, and files are published via a same-directory temporary file.

A run is `COMPLETE` only when every declared transfer and task completes. A
partial run also writes `diagnostics/incomplete-transfers.csv`,
`diagnostics/incomplete-tasks.csv`, and `diagnostics/diagnostic-summary.json`.
The platform entry point decides whether the configured `strict` completion
policy turns that recorded partial result into a non-zero process exit.
