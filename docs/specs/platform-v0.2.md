# Spec: SCP-SatComPlate on ns-3.48

Status: approved for incremental implementation on 2026-08-04.

## Objective

Build the next SatCompute platform on the official ns-3.48 codebase while
preserving the reproducible IPv4 routing, workload, compute, and metrics
semantics of the ns-3.33 implementation. Replace JSON-only orbital playback
with an online ns-3 circular-orbit provider, while retaining deterministic
offline topology traces for audit, replay, and future fault generation.

Success means that one versioned scenario file drives both offline trace
generation and online simulation, shared-time topology snapshots are identical,
existing routing modes remain reproducible, and every migration slice has
focused tests under the established SatCompute test hierarchy.

## Scope

The implementation includes:

- an ns-3.48 'contrib/satcompute' CMake module;
- scenario schema version 0.2 and a typed C++ loader;
- seconds in scenario files with exact integer-nanosecond runtime scheduling;
- stable external satellite IDs and ns-3 circular-orbit positions;
- fixed plus-grid candidate ISLs with distance-gated activation;
- fixed and distance-derived propagation delay modes;
- configurable periodic network-state updates;
- route recomputation only when the effective active-link set changes;
- offline position and topology generation from the same scientific core;
- JSON replay for regression and reproducibility;
- the five existing IPv4 routing modes;
- existing transfer, task, compute-profile, metrics, and diagnostic behavior;
- deterministic manifests and effective-configuration output;
- unit, smoke, and regression coverage in the existing test locations.

The following are deferred:

- frontend/backend transport implementation;
- executable satellite or link fault models;
- IPv6 and SRv6 routing;
- ground stations and feeder links;
- SGP4/TLE or non-circular orbit propagation;
- any change to existing IPv4 hash semantics.

## Accepted Decisions

1. Official ns-3.48 ideal circular orbits are the new online physical baseline.
   Legacy Hypatia/TLE snapshots remain replay and comparison inputs.
2. Online and offline execution call the same orbit and topology policy code.
3. Scenario schema 0.2 is the authoritative experiment input. Large reusable
   inputs remain independent files referenced by the scenario.
4. Scenario instances remain standard JSON. A JSON Schema documents every
   field with a description, units, constraints, and conditional requirements.
5. Human-facing time values use seconds. Values must be finite, non-negative,
   representable as integer nanoseconds, and converted once during loading.
6. Fixed and distance scenarios explicitly provide their update interval;
   values such as 20 s, 1 s, or 2 s are data rather than compiled behavior.
7. Reproducibility means that a fixed complete input produces the same ordered
   route decisions and metrics. Stateful routing may reroute deterministically.
8. 'legacy/ns-3.33' is permanent history. Development uses small branches and
   pull requests from 'main'.

## Architecture

~~~text
scenario-0.2.json
        |
        v
ScenarioConfigLoader ----> effective-config.json + input hashes
        |
        v
ResolvedScenario
        |
        +----> OfflineTraceRunner ----> nodes/topology trace + manifest
        |
        +----> OnlineSimulator
                  |
                  v
              OrbitProvider
              |          |
              |          +-- JsonReplayOrbitProvider
              +------------- Ns3CircularOrbitProvider
                  |
                  v
          FixedCandidateIslPolicy
                  |
                  v
             BaseLinkState
                  |
          [future fault overlay]
                  |
                  v
          EffectiveNetworkState
             |             |
             |             +-- update channel delay
             +-- active edge changed? --yes--> recompute IPv4 routes
~~~

The scientific core does not depend on a frontend protocol. A later exporter
may read immutable state frames without feeding wall-clock timing back into the
simulation.

## Scenario Configuration Contract

The primary invocation is:

~~~bash
./ns3 run "satcompute --scenarioConfig=path/to/scenario.json \
  --outputDir=/tmp/satcompute-run"
~~~

Simulation-affecting values belong in the scenario:

- simulation start and duration;
- constellation and orbital parameters;
- candidate-link policy, seam policy, and distance threshold;
- bandwidth, MTU, queue capacity, and receive buffer;
- delay mode, fixed delay when applicable, and network update interval;
- routing mode, hash seed, and recomputation policy;
- transfer chunking and pacing inputs;
- referenced compute, task, transfer, and future fault traces;
- offline trace cadence and format;
- ns-3 seed, run, and assigned stream ranges when randomness is used.

Operational values may remain CLI options:

- scenario path;
- output directory;
- log verbosity;
- diagnostic collection verbosity;
- run label.

Every supported override appears in 'effective-config.json'. Relative paths
resolve against the scenario file directory. The loader uses closed-world
validation: unknown and missing fields are errors.

The schema uses explicit unit suffixes such as '_s', '_m', '_bps', '_bytes',
and '_us'. Seconds may contain up to nanosecond precision; the resolved form
stores integer nanoseconds.

Conditional network rules:

- 'delay_mode = fixed' requires a positive 'fixed_delay_us'.
- 'delay_mode = distance' requires 'fixed_delay_us = null'.
- 'network_update_interval_s' is always explicit and positive.
- 'routing.recompute_policy' is initially 'on-topology-change'.
- A final update is scheduled at the simulation end only when explicitly
  required by trace export; no event may be scheduled after the stop time.

## Time and Event Semantics

At a periodic network update time t, the controller performs one atomic
transition:

1. Query every satellite position at t.
2. Re-evaluate every fixed candidate ISL against the distance threshold.
3. In distance mode, update propagation delay for active candidates.
4. Apply the future active-fault mask to obtain the effective edge set.
5. Apply all link/interface state changes as a batch.
6. Recompute global routes once only if the effective edge set changed.
7. Allow workload events at t to observe the completed network transition.

In fixed mode, topology is piecewise constant between configured updates and
channel delay remains constant. In distance mode, positions, distance gates,
and channel delays refresh at every configured update. A delay-only change does
not increment the route epoch or rebuild routes under the existing hop-based
IPv4 algorithms.

A future fault or repair is an asynchronous integer-nanosecond event. It
immediately changes the effective edge set and triggers one route rebuild; it
never waits for a periodic update. Subsequent periodic updates regenerate the
base topology and reapply the active-fault mask.

## Orbit and Topology Semantics

- Satellite identity is the stable external '(plane, slot)' mapping defined by
  the scenario, independent of node creation order.
- Existing alternating half-slot 'phase_diff' behavior is preserved and is not
  silently replaced with a Walker Delta F interpretation.
- Candidate neighbors are generated once by the plus-grid policy. An
  inter-plane neighbor is never replaced by a nearer satellite.
- A candidate link is active when its distance is less than or equal to the
  configured maximum and inactive otherwise.
- Fixed delay uses the scenario value. Distance delay divides current distance
  by 299792458 m/s and rounds the one-way result to the nearest integer
  nanosecond; an exact half-nanosecond is rounded upward.
- Offline and online output at identical timestamps has identical external IDs
  and active-edge sets. Coordinates use a documented numerical tolerance and
  canonical serialization.

## Routing and Determinism

The IPv4 migration preserves:

- 'global-first';
- 'global-hash-per-flow';
- 'global-hrw-per-flow';
- 'global-size-aware-hrw';
- 'global-capacity-aware-hrw'.

The legacy 'global-hash-per-flow' 21-byte IPv4 encoding and FNV-1a-64 results
remain a golden compatibility contract. HRW candidate identity, canonical
ordering, sticky assignments, reservations, path admission, and same-time tie
breaking remain deterministic.

Reproducibility requires a fixed scenario, referenced input bytes, code commit,
ns-3 version, hash seed, ns-3 seed/run/streams, and event order. Output
manifests record each value. A future state exporter is read-only and may not
affect simulation scheduling.

## Compute and Workload Inputs

ComputeProfile, TaskTrace, and TransferTrace remain independent versioned JSON
files referenced by the scenario. Static compute placement and capacity are
inputs. Queue, utilization, transfer, and task lifecycle state are outputs.
Future fault availability overlays the static profile rather than rewriting it.

Existing closed-world schemas, canonical sorting, FCFS order, UDP chunking,
pacing, completion policies, and metrics remain compatibility contracts unless
a later versioned specification changes them.

## State Output Boundary

No transport is implemented in this scope. The core retains a read-only state
representation containing at least simulation time, stable satellite ID, ECEF
x/y/z coordinates in metres, active links, current link delays, and optional
compute state.

Version 0.2 writes paired node/topology JSON files plus a hashed manifest. An
exported frame is an `orbit-policy-evaluation`: it samples the continuous orbit
and shared topology policy at the configured trace cadence, independently of
when the live network applies updates. Thus a 1 s trace with a 20 s network
interval contains audit states at 1–19 s while the network still retains its
0 s state. The two paths must agree at every shared timestamp. The manifest,
not a directory scan, is the authoritative slice inventory.

## Tech Stack and Commands

- ns-3.48 with CMake and C++23;
- Python 3.10+ for generation, validation, and analysis;
- standard JSON plus a versioned JSON Schema;
- GitHub branches and pull requests.

CMake 3.25 or newer is required. An ignored repository-local '.venv' may supply
CMake when the system package is older.

Configure and build only the SatCompute module and its required dependencies.
The project configuration and GitHub CI deliberately leave ns-3's global
examples and test suites disabled:

~~~bash
PATH="$PWD/.venv/bin:$PATH" ./ns3 configure --enable-modules=satcompute -G Ninja
PATH="$PWD/.venv/bin:$PATH" ./ns3 build
~~~

Focused SatCompute verification commands after the module exists:

~~~bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
~~~

## Project Structure

~~~text
contrib/satcompute/
|-- CMakeLists.txt
|-- model/                  reusable simulation core
|-- helper/                 ns-3 integration helpers
|-- examples/               runnable platform entry points
|-- config/                 schema, typed loader, effective config
|-- routing/                IPv4 adapter and selection policies
|-- topology/               orbit, candidates, links, controllers
|-- traffic/                transfer engine
|-- task/                   compute and task state
|-- metrics/                structured output
|-- tools/                  offline generation and analysis
|-- input/                  small maintained examples only
+-- tests/
    |-- unit/
    |-- integration/smoke/
    |-- integration/regression/
    |-- fixtures/
    +-- support/

docs/specs/                 approved specifications
docs/plans/                 implementation plans
~~~

## Code Style

Use ns-3 GNU C++ style, explicit units, stable integer time, and Doxygen for
public APIs.

~~~cpp
/**
 * Convert a validated scenario duration to simulation time.
 *
 * @param seconds Human-facing scenario seconds.
 * @return Exact ns-3 time represented in integer nanoseconds.
 */
Time
ToSimulationTime(const DecimalSeconds& seconds)
{
  return NanoSeconds(seconds.ToNanoseconds());
}
~~~

Do not add Unicode mathematical symbols to C++ comments or Doxygen. Python
tools use type hints, immutable parsed configurations, and deterministic
sorting before serialization.

## Testing Strategy

Tests stay under 'contrib/satcompute/tests/':

- unit tests cover schema validation, exact time conversion, orbit phasing,
  candidates, distance gates, delay conversion, hashing, and change detection;
- smoke tests cover one small end-to-end run per delivered vertical slice;
- regression tests preserve route decisions, task results, and metrics;
- fixtures remain small and canonical;
- GitHub CI builds the targeted module and runs only these project-owned tests;
  `test.py` and upstream example tests are not project gates.

High-risk contracts include:

1. A 1 s offline trace downsampled at 20 s equals a 20 s online run at every
   shared timestamp; the same gate also covers a 2 s trace.
2. Repeated runs produce identical normalized topology, route, and metric
   output.
3. Distance-only delay changes do not rebuild routes.
4. One effective edge-set change causes exactly one route recomputation.
5. Legacy fixed-hash golden mappings remain unchanged.

Generated 0.2 traces are also replay inputs. A present manifest is
authoritative: every listed file hash is checked, unlisted stale files are
ignored, embedded pair times must match the selected filename time, and a
periodic update exactly at the simulation stop boundary is not applied.

## Boundaries

Always:

- validate inputs before scheduling events;
- share orbit/topology code between offline and online execution;
- record effective configuration and hashes;
- run focused project tests before commits and the complete project-owned suite
  before PR merge;
- preserve upstream history and module placement in 'contrib'.

Ask first:

- changing schema 0.2 after release;
- adding external runtime dependencies;
- changing legacy hash or workload semantics;
- modifying upstream 'src/' behavior;
- expanding into deferred scope.

Never:

- commit generated experiments, build output, caches, or secrets;
- derive stable satellite IDs from ns-3 node creation order;
- maintain separate online and offline topology formulas;
- recompute routes only because hop-based propagation delay changed;
- delete 'legacy/ns-3.33';
- merge a failing or partially verified increment.

## Success Criteria

- 'main' retains official ns-3.48 ancestry and the legacy branch remains
  independently accessible.
- A documented schema 0.2 drives online simulation and offline trace generation.
- Scenario seconds convert exactly to integer nanoseconds.
- Fixed and distance modes honor their configured update intervals.
- Routes rebuild only on effective edge changes or future asynchronous faults.
- Online and offline topology agree at shared timestamps.
- All five IPv4 modes and existing task/compute behavior pass migrated tests.
- The module builds on ns-3.48 with a targeted SatCompute configuration and
  without globally enabling examples or tests.
- All focused SatCompute unit, smoke, and regression tests pass.
- Every merged feature branch is safely removed locally and remotely.

## Open Questions and Deferred Decisions

No question blocks the agreed implementation. Later specifications will define
the frontend transport, fault trace and in-flight packet semantics, IPv6/SRv6
hash identity, and any SGP4/TLE provider.
