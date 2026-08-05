# SatCompute ns-3.48 migration status

Status: historical v0.2 closeout, superseded on 2026-08-05 by the approved
[v0.3 legacy-parity specification](specs/platform-v0.3.md) and
[implementation plan](plans/ns3-48-legacy-parity.md).

The results below describe the rejected v0.2 architecture and remain only as
an audit record. The active v0.3 migration is not complete.

## Repository baseline

- `main` descends from the official ns-3.48 tag and is the active development
  line.
- SatCompute is isolated in `contrib/satcompute`; upstream `src/` behavior is
  unchanged.
- `legacy/ns-3.33` permanently preserves the previous implementation and is
  not a merge parent of `main`.
- GitHub has one project check named `SatCompute CI`. It configures with
  `./ns3 configure --enable-modules=satcompute -G Ninja`; global ns-3 examples,
  tests, and `test.py` are not enabled or run.

## Delivered contracts

| Area | ns-3.48 result |
|---|---|
| Experiment input | Closed-world scenario schema 0.2 with descriptions, explicit units, exact seconds-to-nanoseconds conversion, resolved paths, effective config, and input hashes |
| Orbit | Official `LeoCircularOrbitMobilityModel` with stable plane-major satellite IDs, Walker Star/Delta RAAN span, legacy optional half-slot phasing, and configurable epoch offset |
| ISL topology | Fixed canonical plus-grid candidates, optional seam, inclusive distance gate, and no nearest-satellite substitution |
| Network cadence | Scenario-controlled periodic updates; fixed and distance modes can independently use values such as 20 s, 1 s, or 2 s |
| Delay | Fixed integer-nanosecond delay or distance divided by 299792458 m/s and rounded to nearest nanosecond with exact halves upward |
| IPv4 routing | `global-first`, legacy fixed-hash per flow, HRW per flow, size-aware HRW, and capacity-aware HRW |
| Workloads | Deterministic direct UDP transfers plus task input transfer, FCFS compute, and result transfer |
| Compute input | Independent closed-world compute profile referenced by the scenario rather than embedded in orbit state |
| Outputs | Effective configuration, routing/transfer/task/compute metrics, partial-run diagnostics, and run summary |
| Offline state | Version 0.2 ECEF node slices, active-link slices, SHA-256 manifest, normal-run export, and `--exportOnly=true` |
| Replay | Legacy slices and self-describing 0.2 traces; a present manifest is authoritative and every listed file hash is verified |

## Time and topology behavior

Continuous circular-orbit positions are available at every ns-3 simulation
time. The configured network cadence controls when the simulated interfaces,
distance-derived delays, and distance gate are refreshed. Between two network
ticks, the network retains the last applied state.

The trace cadence is independent. For example, a scenario can evaluate and
write orbit/topology state every 1 s while applying the network only every
20 s. Those intermediate files are audit/fault-generation inputs, not hidden
network updates. Regression gates prove that 1 s and 2 s traces downsampled at
20 s exactly match a 20 s online run at 0, 20, and 40 s for stable IDs, active
edges, integer delays, and ECEF coordinates within 1e-6 m.

Routes are populated initially and recomputed only when the effective active
edge set changes. A distance-only delay refresh does not rebuild the current
hop-based IPv4 routes. Future asynchronous failure/repair events are specified
to apply immediately at their exact integer-nanosecond time and then trigger
one route rebuild, without waiting for the next periodic tick.

## Determinism

The legacy hash route is fixed for a fixed IPv4 five-tuple, hash seed, and
canonical candidate set. HRW is likewise deterministic. Size-aware and
capacity-aware modes are stateful, but fixed task/transfer inputs and canonical
same-time ordering make their reservations and route choices reproducible;
they do not draw random routes.

The scenario records the ns-3 seed, run number, and reserved stream start.
Current orbit, topology, workload, and routing paths consume no random streams.
Repeated online/export-only runs produce byte-identical topology traces, and
repeated workload runs preserve normalized metrics and routing results.

## Verification maintained in the repository

- Python contract tests cover scenario, transfer/task/compute, and topology
  output schemas.
- C++ unit executables cover exact time conversion, snapshot validation,
  addressing/link transitions, five routing modes, UDP/task/compute behavior,
  online orbit/topology policy, trace export, manifest integrity, generated
  replay, and 1/2 s versus 20 s equivalence.
- Smoke tests exercise validation, legacy replay, online execution, and
  export-only execution.
- Regression tests exercise all five online IPv4 modes, deterministic repeated
  tasks, 66-satellite online construction, normal/export-only trace equality,
  and platform-level replay of a newly generated 0.2 trace.

## Explicitly deferred work

The following items are intentionally not partial implementations in this
migration:

- executable satellite/link fault generation and fault overlays;
- backend/frontend state transport or wall-clock streaming;
- IPv6 routing and SRv6;
- ground stations and feeder links;
- SGP4/TLE and non-circular online orbit providers.

The existing trace and manifest are the intended deterministic input boundary
for later position-dependent fault generation. A later frontend adapter can
consume the same read-only state (`simulation_time_ns`, stable ID, ECEF x/y/z,
active links, and delay) without changing simulation event order.
