# Topology identity and link state

Satellite identity is an external scenario property. `SatelliteIdMap` stores an
explicit bidirectional association and never derives a satellite ID from
`Node::GetId()`. Canonical ordering is ascending external ID, independent of
the order in which ns-3 nodes were created.

IPv4 service addresses occupy `172.16.0.0/12` and follow canonical satellite
order. Each service address retains the legacy dedicated interface, so the
first PointToPoint ISL remains interface 2. ISL networks are allocated as
deterministic `/30` subnets from `10.0.0.0/8` in canonical first-install order.

`SatelliteLinkState` applies a complete active-edge snapshot. It validates and
canonicalizes the entire logical snapshot before changing devices. Removed
links retain their devices, addresses, and output-interface identities while
their IPv4 interfaces are down; restoring the edge brings the same interfaces
back up.

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
ns-3.48 adapter for both `global-first` and deterministic legacy
`global-hash-per-flow`, advancing its route epoch after each recomputation.

The ns-3 `QueueSize` byte counter is unsigned 32-bit. Scenario schema 0.2
therefore rejects `network.isl_queue_bytes` above 4,294,967,295 during loading,
before any network device is created.
