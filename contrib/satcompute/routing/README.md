# SatCompute IPv4 routing

SatCompute installs its own `Ipv4GlobalRouting` subclass through the normal
ns-3.48 routing-helper API. `global-first` delegates packet selection to the
stock global router. `global-hash-per-flow` canonicalizes equal-cost host-route
candidates and selects `FNV-1a-64(seed || IPv4 five-tuple) % candidate_count`.
`global-hrw-per-flow` scores each canonical candidate using the legacy 37-byte
HRW key and selects the highest score. Candidate additions or removals therefore
move only flows whose winning candidate changes, and restoring the same
candidate set restores the same choices.

The hash input remains the legacy 21-byte, big-endian contract. Route decisions
are cached per route epoch, and an epoch advances exactly once when an applied
topology snapshot changes the active edge set. Delay or bandwidth changes alone
do not rebuild routes or advance the epoch.

The remaining two reservation-aware modes are added in subsequent migration
slices; the replay controller rejects them until their state contracts are
available instead of silently falling back to another mode.
