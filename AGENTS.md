# AGENTS.md

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
validated in `satcompute.cc`. The constellation input uses the native ns-3.48
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
Pre-N5C uses one `feature/n5-baselines` branch for full Recompute and real 1+1,
focused/maintained tests, frozen-B R5 compatibility, and R0-R4 online runs.
Stop with a pushed Draft PR to `n5` for baseline audit; do not merge it or enter
N5C. Do not tune workload, faults, deadlines, routing or seed/run for results.
The accepted Pre-N5C START revision overrides only LLM token mapping: 400 WU/token,
total scene WU exactly 352513119 and LLM WU exactly 61333200; whole-token balancing
allows at most ±200 WU per LLM task. Re-run only new R4–R7 with risk-weighted OFF→START;
leave placement, faults, routing, deadlines and old evidence unchanged. Keep PR #97 Draft.
The subsequent user-approved ON correction retries only NO_ADMISSIBLE_PATH pauses on
real capacity release, retaining the fixed pair and ON score; no new fault draws,
reservation bypass or historical checkpoints. Re-run R4-R7 in a new output directory.
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

## Project Overview

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
