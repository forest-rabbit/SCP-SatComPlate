# Topology identity and link state

Satellite identity is an external scenario property. `SatelliteIdMap` stores an
explicit bidirectional association and never derives a satellite ID from
`Node::GetId()`. Canonical ordering is ascending external ID, independent of
the order in which ns-3 nodes were created.

IPv4 service addresses occupy `172.16.0.0/12` and follow canonical satellite
order. Each service address retains the legacy dedicated interface, so the
first PointToPoint ISL remains interface 2. ISL networks are allocated as
deterministic `/30` subnets from `10.0.0.0/8` in canonical fixed-candidate
order.

`SatelliteLinkState` applies a complete active-edge snapshot. It validates and
canonicalizes the entire logical snapshot before changing devices. Removed
links retain their devices, addresses, and output-interface identities while
their IPv4 interfaces are down; restoring the edge brings the same interfaces
back up.

Before simulation starts, the replay controller scans every selected slice and
preinstalls the canonical union of candidate-link devices. Candidates absent
from the initial active snapshot start with both IPv4 interfaces down. Runtime
updates therefore only reconfigure or toggle known interfaces; they never add
a late net device that ns-3 TrafficControl has not initialized. The later
online controller uses the same rule with the plus-grid candidate set.

The update summary separates active-edge changes from attribute changes. A
controller must recompute IPv4 routes only when `ActiveEdgeSetChanged()` is
true. Changing only propagation delay or bandwidth updates the existing channel
and devices without changing the route epoch.

`ReplayTopologyController` selects slices using the scenario duration and
network interval. Scenario bandwidth always overrides legacy snapshot metadata.
In `fixed` mode, `fixed_delay_us` is converted once and overrides every slice;
in `distance` mode, each slice's legacy microsecond delay is converted once to
integer nanoseconds. The controller repopulates routes initially and then calls
one recomputation only for an active-edge change. It installs the SatCompute
ns-3.48 adapter for `global-first`, deterministic legacy
`global-hash-per-flow`, stable `global-hrw-per-flow`, and stateful deterministic
`global-size-aware-hrw` and `global-capacity-aware-hrw`, advancing its route
epoch after each recomputation.
Reservation-aware modes share one controller-owned flow registry so later
workload integration observes the same state at every satellite. The replay
controller also provides the capacity policy with a read-only complete-path
view and invokes route-update callbacks only after the new routes and epochs
are visible.

The ns-3 `QueueSize` byte counter is unsigned 32-bit. Scenario schema 0.2
therefore rejects `network.isl_queue_bytes` above 4,294,967,295 during loading,
before any network device is created.

Deterministic online orbit position and topology-policy slices, including the
independent trace/network cadence semantics and frontend boundary, are
documented under `topology/export/`.
