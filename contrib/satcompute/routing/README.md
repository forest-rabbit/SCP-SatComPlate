# SatCompute IPv4 routing

SatCompute installs its own `Ipv4GlobalRouting` subclass through the normal
ns-3.48 routing-helper API. `global-first` delegates packet selection to the
stock global router. `global-hash-per-flow` canonicalizes equal-cost host-route
candidates and selects `FNV-1a-64(seed || IPv4 five-tuple) % candidate_count`.
`global-hrw-per-flow` scores each canonical candidate using the legacy 37-byte
HRW key and selects the highest score. Candidate additions or removals therefore
move only flows whose winning candidate changes, and restoring the same
candidate set restores the same choices.

`global-size-aware-hrw` keeps one sticky assignment per node and active flow.
The first assignment considers only the flow's two highest HRW candidates and
chooses the one with fewer currently reserved declared bytes (the HRW primary
wins ties). A disappeared candidate releases its reservation immediately and
is reselected deterministically; sender completion releases all remaining
assignments for that flow.

`global-capacity-aware-hrw` performs admission over complete paths in the ECMP
shortest-path graph. It maximizes residual bottleneck bandwidth and uses HRW
rank as the deterministic tie order, reserves the admitted rate on every
directed hop, and waits when no positive-capacity path remains. A topology
callback runs only after the atomic route update, allowing a transfer engine to
release an invalid complete path and re-admit it. Packet forwarding consumes
those sticky path assignments; transition packets use HRW without creating a
partial reservation.

The hash input remains the legacy 21-byte, big-endian contract. Route decisions
are cached per route epoch, and an epoch advances exactly once when an applied
topology snapshot changes the active edge set. Delay or bandwidth changes alone
do not rebuild routes or advance the epoch.

All five legacy IPv4 routing mode names now have explicit ns-3.48 behavior;
none silently falls back to another configured mode.
