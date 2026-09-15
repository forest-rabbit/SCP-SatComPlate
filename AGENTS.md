# AGENTS.md

### Current handoff: protection config hierarchy closed (2026-09-15)

PR #105 merged into n5 as `2d601d268` after the single successful phase CI
`34940108716` on `14d667676` (build/Python/C++/smoke/regression, 7m43s).
Merge tree equals the tested head. The local and remote feature branch were
deleted after ancestry verification; all three implementation commits remain in n5.
This receipt is documentation-only; do not trigger another CI for it.

The separately approved default-profile commit `44d1b6a53` sets formal defaults to
CompFRR adaptive + CompFRR-P + Selective + Relocate, CUMULATIVE and no ablation.
OFF remains an explicit diagnostic/history capability, not a formal comparison scheme.
This default change is not part of `5cf0ac56e`'s pure-refactor equivalence claim.
Historical/test launch adapters freeze omitted old defaults; current formal runner
serializes the complete profile. Baseline private defaults and capabilities stay frozen.

Defaults remain typed `xx = xx;` in `protection/protection-para.cc`, separate from
CLI/validation in `protection-config.cc`. Ordinary INPUT is now
`compfrrInputPolicy=eager|deferred|selective`; old names are explicitly mapped in
test launch/evidence helpers, not production aliases. Preserve historical CB
omitted busy=relocate versus canonical CB busy=recompute. All audited recent-U
run11-15 argv use equals form; archival identity stays read-only/non-executable.
See `docs/n5/reviews/Protection-config-hierarchy.md` for mapping, gates and receipts.
STOP after closeout. Multi-tree is next planned work, not implemented or authorized
yet. Do not run formal 1300s matrices, change algorithms/INPUT/U, create a new branch
or modify main/legacy without the next accepted task. Older stage plans below are history.

### Previous handoff: INPUT repository closed (2026-09-15)

INPUT consolidation is complete on n5: PR #104 merged as `d095ce845` after the single
successful phase CI `34930948459` on `fd29f7aca`. Public INPUT is only
`inputPolicy=eager|deferred|selective` (default eager); selective is frozen SER.
The tracked causal fixture, neutral dependency/lifecycle and production accounting
remain maintained; the one-shot offline stack and public calibration switches are retired.
JIT #99 is CLOSED, not merged. Its five unique commits have a verified/restored Git bundle.
JIT, CB-Sat, worthiness and INPUT runtime feature refs were deleted after archive/ancestry checks.
Only main, n5 and legacy/ns-3.33 remain; main/legacy and formal experiment outputs are unchanged.
See `docs/n5/reviews/INPUT-final-repository-closeout.md` for receipts and archive location.
Older dated stage plans below are historical context, not instructions to resume those branches.
STOP after the closeout receipt. Multi-tree is next planned work only after separate approval;
do not start it, retune INPUT/U, run a new performance matrix or trigger another phase CI.

This file provides guidance to AI agents when working with code in this repository.

## SCP-SatComPlate Project Rules

These project-specific rules override the generic upstream ns-3 guidance below
for all SatCompute branches and GitHub workflows.

The `main` branch is based on the official ns-3.48 tag. The permanent
`legacy/ns-3.33` branch preserves the previous SatCompute implementation and
must not be merged into `main` as an unrelated history.

Project-specific code belongs in `contrib/satcompute/`; do not modify upstream
`src/` modules unless a separately reviewed upstream-compatible change is
required. SatCompute models satellites, inter-satellite links, task compute,
and project-owned fault generation/execution only. Ground stations, feeder
links, the frontend transport, IPv6, and SRv6 are outside the current
implementation scope.

SatCompute-owned tests and fixtures stay under
`contrib/satcompute/tests/{unit,integration,fixtures}`. Each increment
must build, pass its focused tests, and leave the worktree clean before it is
committed. Preserve fixture content unless a task explicitly changes its
contract.

Configure project work with
`./ns3 configure --enable-modules=satcompute -G Ninja`.
Do not enable ns-3's global examples or test suites in SatCompute configuration
or GitHub CI, and do not run `test.py` or upstream example tests there. Project
verification consists of the targeted module build plus the maintained tests
under `contrib/satcompute/tests/`. GitHub CI is a manual phase gate: run it once
on the final integration branch before merging the major phase into `main`, not
for every commit or pull request and not again after the merge. Keep the existing
full regression suites enabled. The separately authorized formal release run is
not repeated by CI. Require passing checks and the user's final manual review
before the phase integration merge; do not bypass repository-required checks.
Focused local builds and tests remain required for every increment.

The current platform contract is documented next to the implementation:
`contrib/satcompute/README.md` defines execution and parameters, while each
module README defines its own behavior and files. Platform execution uses typed
defaults in `para.h`/`para.cc` with optional CLI overrides registered and
validated at the platform boundary (`protection/protection-config.*` owns protection
CLI/validation, with private defaults in `protection-para.*`). The constellation input uses the native ns-3.48
`LeoOrbitalShell` CSV columns and describes orbital structure only. Compute
profiles, tasks, topology slices, and future fault events remain independent
data files. Do not reintroduce a complete scenario JSON, a resolved/effective
configuration layer, schema/software version fields, or duplicate a parameter
across `para.cc` and the constellation file. Human-facing simulation durations
and cadences use seconds and are converted to ns-3 `Time` or integer nanoseconds
only at component boundaries.

The online simulator and topology-only platform mode must share orbit,
candidate-link, distance-gate, delay, and slice-export implementations. Stable
external satellite IDs must not depend on ns-3 `Node::GetId()`. Periodic network
updates always refresh distance-mode delays, but global routes are recomputed
only when the effective active-link set changes. A future fault event is an
asynchronous nanosecond event and will bypass the periodic cadence.

Use small `feature/*`, `refactor/*`, or `docs/*` branches and
pull requests. After a PR is merged and its head is confirmed reachable from
`main`, delete the corresponding local and remote branch. Never delete
`legacy/ns-3.33`.

N5 stage exception (user-approved N5A contract): use `n5`, created from
`main@n4-complete`, as the integration base for Pre-N5 and all N5 feature PRs.
After an approved feature PR is merged, verify its head is reachable from `n5`
before deleting that feature branch. Keep `main` unchanged until the full N5
phase passes review and phase CI. N5A uses one `feature/n5a-protection-runtime`
branch and stops after each G1/G2/G3/G4 gate for user approval. The active
contract is summarized in `contrib/satcompute/protection/README.md`; cL is an
asynchronous generation delay and equivalent cost, never a primary compute pause.

N5A and N5B are complete and merged into `n5` through PR #95 and PR #96.
The 2026-09-12 user-approved Pre-N5C closeout supersedes the Draft-only stop:
merge PR #97 (`feature/n5-baselines`) into `n5` after local verification, then
delete that feature branch only after confirming its head is reachable from `n5`.
Do not merge into `main`, add a tag, run extra GitHub CI, repeat the 32 formal
simulations, or implement N5C. N5C needs its own accepted task contract.
The frozen evidence is `docs/n5/reviews/Pre-N5C-placement-baselines-final.md`:
32 runs at clean execution HEAD `b51cc9d63`, with offline audit `ae93f3679`.
Preserve workload, faults, deadlines, routing, costs, seed/run and raw evidence.
The accepted workload uses 400 WU/token, total scene WU exactly 352513119 and
LLM WU exactly 61333200; whole-token balancing allows at most ±200 WU per LLM task.
Risk-weighted OFF-to-START and InputDeferred remain frozen. ON retries only
NO_ADMISSIBLE_PATH pauses on real capacity release, retaining the fixed pair and
ON score; no new fault draws, reservation bypass or historical checkpoints.
The four placements are ffp/lrl/fa-ffp/fa-lrl (default fa-ffp). Minimal ffp/lrl
use only healthy, idle and structural eligibility, followed by real admission
of one selected candidate. Never retry another candidate within that same
decision; existing later decision/capacity-release events remain allowed.
Four placements affect prefault checkpoint pairs and native R0/R1 single-node
roles; checkpoint post-fault recovery ranking stays frozen. Use current active
loads, not cumulative history. Super* is not the accepted naming.
Computational resource costs must be labeled eq-WU / equivalent cost, with
active overhead and total capacity-equivalent waste reported separately.
Reserved-idle opportunity cost is not actual CPU execution. Raw task progress
stays WU; use W_f-W_L and W_L-W_R for work differences, not dimensionless x-l.
LRL/FA-LRL are strong N5C baselines: compare fairly, without presupposing which
scheme wins or tuning the frozen scene to obtain a desired result.
1+1 requests exactly one resource-constrained replica at first TASK_RUNNING;
failed admission is not retried. Normal replicas are fault-exposed; takeover
and recovery immunity occur only after the complete same-ns fault batch.
Both attempts retain the original compute deadline, not a result-delivery
deadline; the first valid delivered RESULT wins. Planned wait/WU are estimates;
actual wait/WU come only from reservations and executed service.
N5B/N5C formal algorithm experiments
use online `generate`; `validation-replay` remains an N5A execution-test exception,
not a production prediction input. Production policy must not depend on the
shadow validator; test-only comparisons are allowed. Reuse the canonical fault
predictor, current sampling probabilities, state adapter and production cost tiers.

### Active N5C contract (2026-09-13)

The user authorized merging PR #98 into `n5` and continuing N5C on
`feature/n5c-backup-placement-v4`. This supersedes the earlier pre-N5C stop above.
PR #98 is merged at `17c4414dcd61a34a2068a84fefb8ccda7e6b7079`; keep PR #99/JIT
out of this branch and preserve its dependent CB branch. Use the accepted V4 and
eight clarified interfaces recorded in `protection/README.md` and
`docs/n5/reviews/N5B-closeout-N5C-kickoff.md`. N5C ranks only the actual remote after
one read-only FA-FFP reference Frequency solve, retains that local, and never
reranks ON. Do not change the frozen scene, fault RNG, frequency equations,
checkpoint/recovery mechanics, or routing. Run the two strict FA-FFP equivalence
gates, two N5C Eager/Deferred main runs, then three Deferred scoring ablations;
do not repeat the old 32-run matrix or mix JIT into comparisons. No extra GitHub
CI, main merge, release tag or N5C integration merge without final user review.

The subsequent user-approved U audit runs on child `feature/n5c-u-refinement`
from PR #100 head `f5a479ae36c15e356cddd063235b33318856a86e`; keep #100 open.
Gate A only: seed 1, runs 11-15, Deferred FA-FFP/full/noU; reuse verified run 11,
add twelve executions without overwriting V4 evidence. No model/runtime changes,
T threshold, 5% tolerance or automatic Gate B decision. Report paired and whole
cohorts, same-snapshot noU counterfactuals, causal history diagnostics and actual
fault differences; identical fault models/streams need not produce identical
realized faults under online generate. Await joint evidence review before any
recent-U implementation. Keep tests in the existing tree and do not add CI.

Gate A is complete: twelve new runs at clean `625fff908104f4ba14443ed47ee856ec49d40adf`,
three reused runs, all audited. The user increased concurrency from four to eight
without restarting simulations. Evidence in `docs/n5/reviews/N5C-U-multirun-audit.md`
shows mixed full/noU outcomes; both complete 3993/4000. Keep V4 FULL and
NO_MODEL_CHANGE pending joint review; do not repeat the matrix or start Gate B.

The user's subsequent review explicitly authorizes a controlled recent-U experiment
on this same child branch, superseding the Gate A stop, not its mixed findings.
Add RECENT_U without redefining FULL/NO_U. Use a past-only window equal to the
current primary's exact remaining pure compute time, no tunable window/weights.
Use read-only event history; preserve scheduling, R/M, hard feasibility, Frequency,
Recovery, RNG, routing, workload and defaults. Verify small tests and old-variant
equivalence before five Deferred recent-U runs (seed 1, runs 11-15), reusing verified
cumulative/noU evidence. Keep #100/#101 open, no CI/merge/tag/default promotion or
new scenario freeze. Record results and limitations; do not tune after outcomes.

The accepted Rational-U next-step plan supersedes the five-run recent-U trial:
terminate the five user-paused simulations, retain partial outputs as incomplete,
and keep RECENT_U code/history for later review, not as a final candidate here.
On this same branch / Draft #101, first persist a read-only run11 FULL snapshot,
then add independent RATIONAL_U = global_U * H/(H+I), where H is the primary's
exact remainingTimeNs and I is continuous idle since actual normal/recovery busy
ended. No adjustable constants or future information. Preserve FULL/NO_U and
defaults. Reuse completed full/noU formal equivalence gates; verify new small
tests and old fixture equivalence. Run only one new Deferred/relocate Rational-U
seed1/run11, 800 tasks/1300s. Report positive or negative results in
`docs/n5/reviews/N5C-rational-U-main-scenario.md`, then stop for user review.
No exponential, run12-15 expansion, CI, merge, tag or automatic default promotion.
Near-zero thresholds are diagnostics only. Distinguish primary work at fault,
uncheckpointed work and actual execution waste; never double-add these quantities.

Rational-U B0/B1 is now complete: one clean run11 at `99be7b760d04e8ba557ecde83e0c3fb156982950`,
800/800 completed, 83 successful recoveries, all audits passed. Both reused legacy
formal gates and 425-file old fixture equivalence passed. Read
`docs/n5/reviews/N5C-rational-U-main-scenario.md` before continuing: gains are
tail-dominated, not broad superiority; keep default FULL. Stop for user review;
do not launch run12-15/exponential, resume recent-U, merge #100/#101 or run CI.

The 2026-09-14 user review authorizes ONLY four additional Rational-U runs 12-15
on this same branch, reusing Rational-U run11 and all ten FULL/noU runs 11-15.
Freeze all production code/formulas/parameters at the run11 execution, and retain
identical workload, fault configuration/streams, recovery contract and routing.
Online realized faults may differ causally; report differences, never force replay.
Audit five-run three-way completion, busy, recovery actions, paired catch, actual
WU/eq-WU waste, network and assignment/storage concentration. Predefine leave-one-out
by the largest beneficial and largest absolute task contribution separately for
catch and total waste, per run and pooled; one identity is (run,task), not an ID
removed from all runs. This is statistical exclusion, not a new simulated scene.
Only test/audit tooling and concise review/handoff documentation may change.
Keep #100/#101 open and #101 Draft; no CI, merge, exponential, parameter tuning or
automatic replacement of cumulative-U. Stop after reporting the five-run evidence.

The four additional runs are complete at clean `b7eb7331c7ea3b6fb7f7724cd151e35bcc5dc521`;
all fifteen outputs passed the read-only audit. Read `docs/n5/reviews/N5C-rational-U-multirun.md`:
all groups complete 3993/4000, but Rational-U busy is 8/409 versus 5/409 for both baselines,
and paired catch/total eq-waste are worse. Tail exclusion remains outcome-sensitive.
Keep default FULL and both PRs open (#101 Draft); do not run more simulations, tune,
merge, promote, or start exponential/recent-U without a new user decision.

## Project Overview

### Recovery deadline correction (2026-09-14, active)

The user accepted `CompFRR_Relocate_Deadline_Feasibility_Audit_Taskbook.md` and
four review corrections. Work on `fix/recovery-deadline-feasibility` from clean
`c1a8704cd`; preserve the two open N5C PRs and all old outputs. Remote-first now
requires full compute-deadline feasibility, using one pure estimator for direct
and migration paths. Busy policy also controls DIRECT_DEADLINE_INFEASIBLE;
INPUT_PATH_UNAVAILABLE may search readable-checkpoint migration rather than
early-returning to recompute. No U/Frequency/RNG/routing/INPUT/deadline/scene changes.
Historical candidate evidence may be UNKNOWN, never false proof of no candidate.
Rerun any formal execution whose recovery behavior changes, even when both paths
ultimately fail; keep unaffected results with equivalence evidence. Audit CB
augmented relocate separately; no automatic main-CB modification. Keep one recovery
attempt, stable-ID migration search and LocalDelivery. No CI/merge/tag/default
promotion or unrelated architecture work. Record concise results beside N5 reviews.

The subsequent user scope reduction authorizes only three corrected run11 formal
executions: FULL, noU, Rational-U, on the same clean commit. Do not run run12-15,
FA-FFP, noR/noM or CB. Preserve the complete read-only historical audit; out-of-scope
affected historical runs remain uncorrected, not newly certified results.

The three corrected run11 executions are complete at clean `bc721ed42`, all 800/800.
Read `docs/n5/reviews/Recovery-direct-deadline-feasibility.md`: task140 migrates 15->0
and meets the unchanged deadline; noU/Rational-U retain every historical CSV field.
Default FULL remains unchanged. Do not expand the matrix, merge/push/CI/tag or promote
another U without a fresh user decision; unrerun historical multi-run evidence remains historical.

The latest user authorizes completing ONLY the U five-run comparison: eight new
1300s executions (FULL 12/14; noU and Rational-U 12/14/15), reusing the three
corrected run11 results and four audited unaffected historical runs. INPUT_PATH_UNAVAILABLE
early-return impact counts as well as direct deadline infeasibility; run15 task53
must not be classified unaffected for noU/Rational-U. Freeze production at bc721ed42;
only test/audit tooling and concise handoff/review docs may change. Use online generate,
retain old evidence, audit all fifteen outputs, three-way common valid catch and
symmetric largest-positive/largest-negative single-(run,task) exclusions. Do not run
other baselines/ablations, change U/defaults, push/merge/CI/tag or start another phase.
Stop after reporting the corrected five-run evidence for user review.

The eight new U runs are complete at clean `a5b00962c`, all full 1300s and return 0.
All fifteen outputs passed audits (including both corrected fallback branches).
Read the five-run section of `docs/n5/reviews/Recovery-direct-deadline-feasibility.md`:
FULL/noU/Rational-U complete 3996/3998/3997 of 4000; common catch is 338.266/341.329/353.387 ms,
busy 5/5/9 of 409. Rational-U's 0.527% total eq-waste gain is tail-sensitive, not a
stable overall replacement advantage. Run15 task53 now migrates 41->0 and completes
for noU/Rational-U. Keep FULL default; recommend stopping this U refinement pending
user review. No further runs, model changes, push/merge/CI/tag or architecture cleanup.

### Checkpoint maintenance correction (2026-09-14, active)

The user accepted the checkpoint maintenance taskbook and five amendments. Work on
`fix/checkpoint-maintenance-semantics` from `08236b8af`. This supersedes the previous
stop only for maintenance audit, minimal repair and affected U validation. Remove
ComputeService idle as an ON-maintenance prerequisite only; retain START and recovery
availability/idle admission. Separate genuine policy PAUSE from local-capture and
remote-batch resource blocks, retaining the last committed cadence/quota during
temporary resource rejection. Preserve bounded storage, actual failed-flow bytes,
continuous records, already-created generation/transfer/merge, strict pre-fault state
and F3 invalidation. Never invent historical captures or skip a missing receipt.
Keep existing parallel INPUT/state/tail recovery, Frequency mathematics, placement,
U/defaults, faults/RNG, routing, scene and deadlines unchanged. Historical KEEP requires
maintenance-trajectory AND resource-ledger equivalence, not merely no fault after pause;
missing historical fields are UNKNOWN. Audit first and determine affected formal U runs;
do not expand other baselines or auto-push/PR/CI/merge/tag. Keep PR #100/#101 open and
all prior outputs intact. Use one concise review document rather than duplicate reports.

Maintenance closeout (2026-09-14): implementation and all 15 full 1300s U runs PASS
at clean execution commit `c7889de89`; outputs `output/checkpoint-maintenance-fixed/`,
report `docs/n5/reviews/Checkpoint-maintenance-semantics-audit.md`. FULL/noU/Rational-U
each complete 4000/4000 tasks with 409 valid recoveries and zero Recompute. Mean catch
is 322.003/324.356/323.636 ms; total equivalent waste is 15.337524/15.433674/15.403837
million eq-WU. Targeted build, maintained C++/smoke/Python tests and independent
accounting/Frequency/N5C/Rational history audits pass. FULL remains default. Stop
for user review; no further runs, parameter changes, push/PR/CI/merge/tag authorized.
Prior U outputs are historical, replaced for current comparisons by the 15 new runs;
other affected baselines have not been revalidated and must not be mixed with them.

### Active N5R contract (2026-09-14)

The user approved the architecture audit and the staged N5R implementation.
PR #102 integrates the complete corrected chain into n5 at `d26f7af90`; N5R uses
one branch `refactor/protection-architecture-consolidation` from that corrected
base, never the older n5. This supersedes the earlier N5C/maintenance stop only
for the approved consolidation. Preserve #100/#101 and experiment history;
clean superseded branches only after inclusion and PR dependency checks.
Keep JIT/V7, main, legacy and formal outputs unchanged.

Follow `docs/n5/reviews/N5R-implementation.md` and the approved architecture audit:
common substrate, F/INPUT B, P, optional recovery/relocation, baselines, reusable
test helpers, canonical docs, then dependency-checked historical archival.
Each executable increment must build and pass unit/contract/small deterministic
semantic-equivalence gates before committing and continuing. Any unapproved
semantic difference stops that increment for a separate audit, not a refactor fix.
Preserve one Frequency solver, START/ON placement contracts, RNG, routing, schema,
actual/planned WU/bytes/storage, deadlines and the current maintenance lifecycle.
No new formal 1300s matrix, N6/N7 experiment, Multi-tree, U tuning or INPUT timing.

User clarification: production compute-pressure policies are only CUMULATIVE and
IDLE_AWARE. noU remains an ablation, not a third pressure policy. Remove recent-U
from the production CLI with an explicit deprecation test; preserve its historical
evidence and dependent implementation until dependency audit permits archival.
All other legacy CLI/CSV/fixture/analysis interfaces remain compatible. CI keeps
the existing manual phase cadence, not per commit. Do not merge N5R or main, or
tag a release, without the user's subsequent final review.

N5R final closeout amendment: the user approved the final architecture taskbook.
Keep this branch / Draft #103. Complete baseline/checkbullet (including its frozen
profile/tools), baseline/recompute and baseline/one-plus-one as unique owners;
shared baseline placements move to policy/placement. Multi-tree gets a README
placeholder ONLY. Preserve public ns3 header names, evidence, model parameters,
source guards and all execution semantics. Pass the existing small N5R gates
before the second stage: read-only offline diagnosis of historical V7 run11 and
its same-batch Deferred anchor. Reuse audit helpers; never bring production JIT
into this branch or run a new formal matrix. Byte classification precedence is
used, no-fault, wrong-target, then failed/cancelled, with independent reason flags.
Non-critical INPUT does not imply no prefetch benefit; sent fraction is not READY.
Unknown evidence stays unknown. Paired contrasts are not exact counterfactuals.
Record both stages in docs/n5/reviews/N5R-implementation.md and stop for review
after the offline report; no V8, optimizer, budget, threshold or new timer.

### Frozen INPUT contract

Only public `compfrrInputPolicy=eager|deferred|selective` remains; the authorized
formal default is selective (historical default was eager). Selective
uses the Deferred checkpoint layout and one frozen SER break-even selector.
Preserve neutral INPUT dependency/lifecycle, actual receiver completion and full-flow
byte accounting. No JIT/NET runtime policy, empirical tuning or second Frequency solver.
Use the tracked 409-candidate SER fixture (68/405 network SEND plus 4 LocalDelivery),
not an ignored calibration output. The retired public switches and large START JSON
are not runtime interfaces. Historical metadata readers may normalize old names only
for exact equivalence checks. See docs/protection/compfrr-f.md and the final INPUT report.

ns-3 is a discrete-event network simulator for Internet systems, written in C++ with Python bindings. The project uses CMake for building but provides a custom `ns3` wrapper script for easier command-line usage.

## Build System

### Essential Commands

**Configuration:**

```bash
./ns3 configure --enable-modules=satcompute -G Ninja  # SCP-SatComPlate setup
./ns3 configure --help                            # Show all options
```

**Building:**

```bash
./ns3 build                                       # Build entire project
./ns3 build [target]                             # Build specific target
```

**Running:**

```bash
./ns3 run "simple-global-routing"                  # Run example program
./ns3 run "program [args]"                       # Run with arguments
```

**Testing:**

```bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

**Utilities:**

```bash
./ns3 clean                                      # Clean build artifacts
./ns3 show targets                               # List available targets
```

## Architecture

### Core Structure

- **`src/`** - 49 official modules (core, network, internet, wifi, lte, etc.)
- **`contrib/`** - Third-party modules
- **`examples/`** - Example programs organized by topic
- **`scratch/`** - User simulation scripts
- **`utils/`** - Development utilities

### Module Organization

Each module follows this structure:

```text
src/module-name/
├── CMakeLists.txt      # Build configuration
├── model/              # Core implementation (.cc/.h)
├── helper/             # Helper classes for easier usage
├── test/               # Unit tests
├── examples/           # Usage examples
└── doc/                # Documentation
```

### Key Design Patterns

- **Attribute System**: Configuration through TypeId attributes
- **Callback System**: Event-driven programming with Callback<> templates
- **Smart Pointers**: Extensive use of Ptr<> for memory management
- **Logging**: Hierarchical logging system with NS_LOG_*
- **Tracing**: Packet and event tracing for analysis

## Development Workflow

### Creating New Modules

```bash
./utils/create-module.py module-name    # Generate module template
```

### Code Style

The full coding style guide is documented in `doc/contributing/source/coding-style.rst`.

- **Clang-format**: Formatting rules and current C++ standard alignment are in .clang-format
- **No Unicode symbols**: Do not use Unicode mathematical symbols (e.g., ≤, ≥, ×, ÷, ∑, π) or arrows (e.g., →, ←, ⇒, ↑) in comments or Doxygen documentation; use ASCII equivalents instead (e.g., `<=`, `>=`, `*`, `/`, `->`, `=>`)

### Sphinx/reStructuredText Documentation

Full guidelines for writing model documentation in Sphinx reStructuredText are in `doc/contributing/source/models.rst`. An empty outline with the required section structure and elements for a new module can be found in `utils/create-module.py`. Note that existing modules are not yet universally conformant to this structure; when adding Sphinx documentation to an existing module, find an appropriate insertion point within the existing structure rather than restructuring the whole document.

### Doxygen Documentation

Full conventions are in `doc/contributing/source/coding-style.rst` (Comments section). Key rules:

- **Coverage**: All classes, methods, and member variables must have Doxygen comments. Exceptions: methods inherited from a parent class (docs are copied automatically) and default constructors/destructors.
- **Comment style**: Use C-style Javadoc blocks (`/** ... */`). Use `///< brief description` for inline member variable documentation.
- **Tag delimiter**: Use `@` not `\` for all tags (e.g., `@param`, `@return`, `@brief`). clang-format recognizes `@` as a Doxygen tag delimiter and formats accordingly.
- **Parameters and return values**: All must be documented with `@param` and `@return`.
- **Cross-references**: Use `@see` for cross-referencing other classes or methods.
- **Internal comments**: Use `@internal` / `@endinternal` for documentation not intended for public Doxygen output.
- **Grouping**: Use `@defgroup` and `@ingroup` to bind logically related classes within a module. Test classes should form an ancillary group with `@ingroup tests`.

Example:

```cpp
/**
 * Brief description of the class.
 */
class MyClass
{
  public:
    /**
     * Constructor.
     *
     * @param n Number of elements.
     */
    MyClass(int n);

    /**
     * Do something useful.
     *
     * @param x Input value.
     * @return Result of the operation.
     */
    int DoSomething(int x);

  private:
    int m_count; ///< Number of elements
};
```

### Testing Requirements

- SatCompute must build from a targeted configuration without globally enabling
  ns-3 examples or tests.
- Maintained tests under `contrib/satcompute/tests/` must pass before commits.
- GitHub workflows must not run `test.py` or upstream example tests.

### Commit Guidelines

- Present tense, imperative mood ("Add feature" not "Added feature")
- Reference modules: "core, network: Add new feature"
  - if list of edited modules spans more than two, suppress listing
- 72 character limit for first line
- Use "(fixes #issue)" for bug fixes, after the list of modules but before
  the summary of the commit, where `#issue` is the number of the issue.

## Common Development Tasks

### Running Single Tests

```bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
```

Tests can be run with ns-3 logging enabled via the `NS_LOG` environment variable:

```bash
NS_LOG="BulkSendApplication" ./ns3 run 'test-runner --suite=applications-bulk-send'
```

Log level can be tuned to reduce noise. Note that filtering to a specific log level deselects prefix information, so `prefix_all` must be added to recover it:

```bash
NS_LOG="BulkSendApplication=level_info|prefix_all" ./ns3 run 'test-runner --suite=applications-bulk-send'
```

### Debugging

```bash
./ns3 run "program --help"              # Show program options
./ns3 run program --command-template "gdb --args %s"  # Run with gdb
```

### Building Specific Modules

```bash
./ns3 build core                        # Build only core module
./ns3 build wifi-simple-adhoc           # Build specific example
```

## Key Files

- **`.ns3rc`** - Build configuration settings
- **`CMakeLists.txt`** - Build system configuration
- **`test.py`** - Main test runner
- **`utils/create-module.py`** - Module generator
- **`.clang-format`** - Code formatting rules

## Module Dependencies

When working with modules, understand the dependency hierarchy:

- **core** - Base classes, logging, attributes
- **network** - Packets, nodes, devices
- **internet** - IP, TCP, UDP protocols
- **applications** - Application layer protocols
- **wifi/lte/etc.** - Technology-specific implementations

## Python Bindings

Python bindings are available but may not cover all C++ APIs. Use:

```python
from ns import ns
help(ns.ClassName)  # Get class documentation
```

## Documentation

Rendered (current release):

- **Manual**: <https://www.nsnam.org/docs/manual/html/>
- **API Reference**: <https://www.nsnam.org/docs/doxygen/>
- **Model Library**: <https://www.nsnam.org/docs/models/html/>

Source (in-tree, for editing):

- **Manual**: `doc/manual/source/`
- **Model docs**: `doc/models/source/`
- **Contributing guide**: `doc/contributing/source/`
