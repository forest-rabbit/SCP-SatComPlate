# ComputeProfile and TaskTrace 0.1

`compute-profile.schema.json` and `task-trace.schema.json` are the complete
field references for compute capacity and task workloads. Both inputs are
closed-world JSON: every property is documented, unknown properties are
rejected, referenced satellite IDs must exist, and input array order has no
semantic effect.

Compute capacity remains scenario input rather than orbit state. A profile
maps each compute-capable satellite to a positive constant rate in abstract
work units per second. The loader sorts nodes by stable external `node_id`, so
the same profile is deterministic even when its JSON array is reordered.

A task moves through this fixed lifecycle:

`PENDING -> INPUT_TRANSFERRING -> QUEUED -> RUNNING -> RESULT_TRANSFERRING -> COMPLETED`

The input transfer runs from `source_node_id` to `compute_node_id`; the result
transfer runs from `compute_node_id` to `result_node_id`. The source and result
may be the same satellite, but each network transfer must have distinct
endpoints. For task ID `T`, the platform derives stable transfer IDs `2*T-1`
and `2*T`; these IDs and all UDP fields remain runtime state and must not be
duplicated in the JSON.

`arrival_time_ns` preserves the legacy sub-second workload contract. All
platform-facing scenario intervals remain seconds in `scenario.schema.json`,
and the C++ runtime represents both as exact integer nanoseconds.

## Compute semantics

Each compute node is a single-server, non-preemptive FCFS queue. Service time
is calculated without floating point as:

`ceil(compute_work_units * 1,000,000,000 / compute_rate_work_units_per_second)`

Tasks are ordered by `(queue_enter_time_ns, task_id)`. Dispatch is deferred to
the end of the current ns-3 event batch, so tasks whose input transfers finish
in the same nanosecond are ordered by task ID regardless of callback order.
There is no random compute scheduling state.
