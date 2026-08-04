# Implementation Plan: SCP-SatComPlate ns-3.48 Migration

## Overview

Deliver the approved platform specification through small, reviewable branches.
Every phase leaves 'main' working, keeps tests in the established SatCompute
hierarchy, and removes its feature branch after merge. The permanent
'legacy/ns-3.33' branch is reference material, not a merge parent.

## Architecture Decisions

- Preserve official ns-3.48 ancestry on 'main'.
- Keep project code in 'contrib/satcompute'.
- Make scenario schema 0.2 the sole semantic input.
- Parse human-facing seconds once into integer nanoseconds.
- Share online and offline orbit/topology implementations.
- Preserve existing IPv4 routing hashes and stateful route behavior.
- Recompute routes only when the effective active-edge set changes.
- Defer frontend transport, faults, IPv6, and SRv6.

## Phase 0: Repository Foundation

### Task 0.1: Publish clean histories

Acceptance:

- 'main' points to the official ns-3.48 tag.
- 'legacy/ns-3.33' points to the prior SatCompute main commit.
- GitHub's default branch is 'main'.

Verification:

- 'git ls-remote --heads origin'
- 'git merge-base main legacy/ns-3.33' reports unrelated histories.

Dependencies: none.

### Task 0.2: Record rules, specification, and plan

Acceptance:

- Project rules cover scope, tests, time, determinism, and branch cleanup.
- The specification covers required contracts and boundaries.
- '.gitignore' combines ns-3.48 and SatCompute protections.

Verification:

- 'git diff --check'
- Markdown and link review.

Dependencies: Task 0.1.

### Checkpoint 0

- Official ns-3.48 ancestry and the two published branch heads are verified.
- Repository rules explicitly limit GitHub verification to SatCompute-owned
  builds and tests.
- Foundation PR is merged and its branch is deleted.

## Phase 1: Module and Configuration Foundation

### Task 1.1: Add a minimal CMake module and platform executable

Create the smallest module and module-owned executable without modifying
upstream source or relying on ns-3's global examples switch.

Acceptance:

- ns-3 discovers 'contrib/satcompute'.
- A topology-free smoke executable exits successfully.

Verification:

- './ns3 configure --enable-modules=satcompute'
- './ns3 build'
- the project smoke runner invokes the module-owned executable successfully

Dependencies: Checkpoint 0.

### Task 1.2: Add schema 0.2

Acceptance:

- Every property has a useful description and explicit units.
- Fixed/distance conditional rules and closed-world objects are encoded.
- A maintained 66-satellite example validates.

Verification:

- Unit tests cover valid, missing, unknown, and invalid conditional fields.

Dependencies: Task 1.1.

### Task 1.3: Add typed C++ loading and exact time conversion

Acceptance:

- Scenario seconds convert to exact integer nanoseconds.
- Relative paths resolve from the scenario file.
- Invalid or sub-nanosecond times fail before simulation setup.

Verification:

- C++ tests cover integer, fractional, boundary, and invalid times.
- A loader smoke prints canonical resolved configuration.

Dependencies: Task 1.2.

### Task 1.4: Write effective configuration and input hashes

Acceptance:

- Every run writes canonical effective configuration.
- Referenced inputs include stable content hashes.
- Operational CLI values are recorded separately from semantic values.

Verification:

- Repeated runs produce identical normalized manifests except documented
  volatile fields.

Dependencies: Task 1.3.

### Checkpoint 1

- Module, schema, loader, and manifest checks pass.
- Upstream tests remain green.
- Configuration PRs are merged and their branches are removed.

## Phase 2: Legacy Static Topology on ns-3.48

### Task 2.1: Port snapshot types and strict JSON readers

Acceptance:

- Existing small node/topology fixtures load unchanged.
- Satellite and link ordering is canonical.
- Unknown fields and mismatched IDs fail early.

Verification:

- Snapshot unit tests and fixture-hash audit.

Dependencies: Checkpoint 1.

### Task 2.2: Port stable IDs, devices, addressing, and link state

Acceptance:

- External IDs map independently of 'Node::GetId()'.
- ISL devices, IPv4 addresses, MTU, queues, and bandwidth match legacy.
- Full snapshots apply atomically.

Verification:

- Four-node static topology smoke and address golden output.

Dependencies: Task 2.1.

### Task 2.3: Port fixed and distance snapshot delays

Acceptance:

- Fixed delay matches the configured value.
- Distance delay uses the documented one-way formula.
- Delay-only changes do not rebuild routes.

Verification:

- Focused delay and route-recompute-count tests.

Dependencies: Task 2.2.

### Checkpoint 2

- Static and dynamic JSON-replay topology smokes pass on ns-3.48.
- Topology PRs are merged and their branches are removed.

## Phase 3: IPv4 Routing Compatibility

### Task 3.1: Port global-first and hash-per-flow

Acceptance:

- Candidate extraction is canonical on ns-3.48.
- Legacy FNV-1a-64 golden selections remain exact.

Verification:

- Routing unit tests and diamond-topology smoke.

Dependencies: Checkpoint 2.

### Task 3.2: Port HRW-per-flow

Acceptance:

- Candidate identity and tie breaking match legacy.
- Candidate add/remove stability tests pass.

Verification:

- HRW golden and dynamic-topology smoke.

Dependencies: Task 3.1.

### Task 3.3: Port size-aware HRW

Acceptance:

- Declared-byte reservations and sticky assignments match legacy.
- Invalid candidates release and deterministically reselect.

Verification:

- Size-aware smoke and replay regression.

Dependencies: Task 3.2.

### Task 3.4: Port capacity-aware HRW

Acceptance:

- Complete-path admission, bottleneck pacing, waiting, release, and re-admission
  match legacy.

Verification:

- Static, parallel, same-epoch, and recovery smokes.

Dependencies: Task 3.3.

### Checkpoint 3

- All five IPv4 modes pass golden tests.
- Routing PRs are merged and their branches are removed.

## Phase 4: Workload, Compute, and Metrics

### Task 4.1: Port transfer input and engine

Acceptance:

- Transfer schema, canonical order, UDP chunking, and pacing match legacy.

Verification:

- Transfer smoke and summary golden checks.

Dependencies: Checkpoint 3.

### Task 4.2: Port compute profile and task state machine

Acceptance:

- Static profiles, FCFS, service-time rounding, and result transfers match
  legacy.

Verification:

- Single-task, FCFS, and heterogeneous-compute smokes.

Dependencies: Task 4.1.

### Task 4.3: Port metrics and diagnostics

Acceptance:

- Structured outputs and failure diagnostics retain their contracts.
- Run summaries reference effective configuration and hashes.

Verification:

- Task, workload, routing, and diagnostic regressions.

Dependencies: Task 4.2.

### Checkpoint 4

- The ns-3.48 JSON-replay platform matches the ns-3.33 behavioral baseline.
- Workload PRs are merged and their branches are removed.

## Phase 5: Online Circular-Orbit Topology

### Task 5.1: Implement identity and phasing

Acceptance:

- Stable plane/slot IDs and alternating half-slot phasing match the scenario.
- No implicit Walker Delta F substitution occurs.

Verification:

- Position and phasing tests at selected timestamps.

Dependencies: Checkpoint 4.

### Task 5.2: Implement fixed-candidate distance gating

Acceptance:

- Candidate identities remain fixed.
- Links activate only at or below maximum distance.
- No nearest-neighbor substitution occurs.

Verification:

- Candidate, threshold, seam, and canonical-order tests.

Dependencies: Task 5.1.

### Task 5.3: Implement periodic online network control

Acceptance:

- Configured fixed or distance intervals drive atomic updates.
- Distance delays refresh every tick.
- Routes rebuild exactly once only when the effective edge set changes.

Verification:

- Recompute-count, delay-only, edge-change, and end-time tests.

Dependencies: Task 5.2.

### Checkpoint 5

- Small and 66-satellite online scenarios run deterministically.
- Online-topology PRs are merged and their branches are removed.

## Phase 6: Offline Trace and Equivalence

### Task 6.1: Add offline snapshot export

Acceptance:

- Export uses the same orbit and topology policy as online execution.
- Nodes and active links serialize canonically with manifest metadata.

Verification:

- Snapshot schema and repeated-export determinism tests.

Dependencies: Checkpoint 5.

### Task 6.2: Add online/offline equivalence gates

Acceptance:

- A fine offline trace equals a coarse online run at common timestamps.
- Active edges compare exactly; coordinates use the documented tolerance.

Verification:

- 1 s versus 20 s and 2 s versus 20 s regression matrices.

Dependencies: Task 6.1.

### Task 6.3: Migrate maintained tools, fixtures, and documentation

Acceptance:

- Supported generation and analysis tools use schema 0.2.
- Existing tests remain in their established locations.
- Legacy Waf commands are replaced with ns-3.48 CMake commands.

Verification:

- Python unit discovery, all smoke runners, and full regressions.

Dependencies: Task 6.2.

### Checkpoint 6: Complete

- The targeted SatCompute configuration and build succeed without globally
  enabling ns-3 examples or tests.
- All SatCompute unit, smoke, and regression suites pass.
- Online/offline topology equivalence passes.
- Worktree is clean and merged feature branches are removed.
- Deferred work is documented without partial implementation.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Unrelated Git histories | High | Preserve legacy independently; port project files only |
| ns-3 routing API changes | High | Port one mode at a time with golden decisions |
| Orbit phasing drift | High | Explicit IDs and phasing tests before topology integration |
| Online/offline divergence | High | One implementation plus equivalence tests |
| Floating threshold differences | Medium | Version constants and exact active-edge tests |
| Fine traces create many files | Medium | Keep compatibility, add chunking only if required |
| CI scope expands into upstream examples | Medium | Target the contrib module and project-owned tests only |
| Stateful routing seems random | Medium | Canonical events, seeds, and decision traces |

## Branch and PR Protocol

1. Start each slice from current 'main' as 'agent/<short-description>'.
2. Stage only the files belonging to that slice.
3. Run focused build/tests and 'git diff --check'.
4. Commit in imperative mood with an ns-3-style subject.
5. Push and open a draft PR with scope and checks.
6. Mark ready and merge only after its checkpoint passes.
7. Verify the PR head is reachable from updated 'main'.
8. Delete the merged local and remote 'agent/*' branch.
9. Never delete 'legacy/ns-3.33'.

## Open Questions

No question blocks execution. Frontend transport, fault execution, IPv6/SRv6,
and high-fidelity orbit providers require separate future specifications.
