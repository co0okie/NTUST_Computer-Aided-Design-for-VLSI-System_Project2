# Clock Buffer Insertion

C++17 solver for CAD Project 2. It reads driving limits, a buffer library and
fixed pins, then writes a legal levelized clock tree. Runtime dependencies are
limited to the C++ standard library.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/cbi example/input.cbi /tmp/output001.cbi
```

The first two arguments are input and output paths; relative paths are supported.
Success prints `T_max`, `T_min`, and `Score`. Failures print a diagnostic to stderr
and return nonzero. Parsing and tree validation finish before the output file is
opened, so rejected instances preserve any existing output.

## Construction and optimization

The solver partitions sinks spatially, builds a binary tree from the leaves,
and searches integer Manhattan diamond intersections for balanced merge
positions. Long edges are subdivided with interior relay buffers along shortest
paths. Boundary-aligned connections can first enter the chip through an interior
relay when no shortest-path buffer position is available. Each parent obeys both
its fanout limit and the **sum** of outgoing wire
lengths. After routing, each buffer receives the cheapest compatible library type.
Relay spacing policies include the original largest length, the cheapest type,
the lowest cost/length ratio, and additional undominated cost/length choices
(up to eight distinct lengths). Each policy constructs independent complete
candidates, which compete on actual Score. These rankings do not prove global
optimality or cover every mixed-type chain. Source connection paths may use a
bounded detour search when their shortest-path relay positions are occupied.
A minimum required relay count rejects routes exceeding the component budget
before inserting their buffers.

Two additional candidates mix relay types along a chain using the coverage-cost
recurrence `C(r) = min_t(cost_t + C(max(0, r - length_t)))`. Equal-cost plans
prefer longer or shorter steps in separate candidates. Each actual placement
updates the remaining distance, so blocked ideal positions can trigger replanning.
The table is capped at distance 4096 and 262,144 transitions per Builder; larger
queries use the original maximum-length routing policy. Placement feasibility
and the final weighted Score are still checked on the complete tree. This bounded
planner does not prove optimality for chains with occupied points or detours.

Binary tree construction also compares up to four branching length policies,
including the original maximum length and the cheapest compatible branching type.
Only types with fanout at least two and length at least two are eligible. Each
branching policy allocates its total outgoing budget across both child routes,
allowing cheap branch buffers followed by relays. These combinations apply to
binary/parity trees only; source star and hub searches run once per relay policy.

Two merge strategies, a direct SRC-to-sinks tree, and separate relay chains from
SRC to each sink, and a single multi-output buffer hub are compared using the actual
objective `w_skew * (T_max - T_min) + buffer_cost`. Every candidate is validated
for unique coordinates, fixed pins, interior buffers, connectivity, tree
structure, and driving limits. Output levels are generated from tree depth.

A final local pass bypasses a child buffer, joining its outputs directly to its
parent. It also tries moving a buffer parent to the removed child's position.
Every trial reselects compatible buffer types and validates the complete tree;
only a strict reduction in weighted Score is accepted. Each pass chooses the
best trial, preserving balanced alternatives when another bypass raises skew.
The search handles trees of at most 512 components, with at most 16 passes and
2048 total trials; the trial budget shrinks with tree size and library size.
This permits local multi-output merges but remains a bounded heuristic.

All integer Manhattan paths between fixed endpoints have the same length parity.
Consequently, sinks with mixed `(x+y) mod 2` cannot have exact zero skew. One
strategy balances virtual sink arrivals with parity offsets to target skew 0
for uniform parity and skew 1 for mixed parity. Separate chains support libraries
containing only fanout 1 buffers when SRC can drive the required number of branches.
Near sinks are connected first so short direct branches release budget for longer
chains. The hub strategy samples sink medians, their bounding-box center, and
near-source positions; it checks the full fanout and total length before selecting
a type. Each eligible hub is then connected to SRC in independent construction
state, all buffer types are reselected, and the complete tree is validated and
scored. The hub-only Score is used solely as a lower bound for pruning because
all sinks share the source-to-hub path and relay insertion only adds cost.
Every completed strategy competes on its validated Score.
This is a heuristic: merge
placement and relay searches are bounded, and feasibility or optimality is not
guaranteed. A failed construction reports an error; it does not prove the input
has no solution. Construction is capped at 100,000 total components.

On the supplied `example/input.cbi`, the initial buffered solver produces four
SMALL buffers, `T_max=143`, `T_min=142`, and `Score=41`. The original direct-only
implementation could not handle this input because SRC fanout exceeded its limit.

## Verification and tester workflow

```sh
ctest --test-dir build --output-on-failure
# or, with Python 3:
python3 tests/test_solver.py build/cbi
```

Tests independently check serialized output, arrival times, cost, and all tree
constraints. They cover uniform/mixed parity, relay insertion, boundary pins,
40 deterministic random sparse inputs, unary buffer libraries with source branching,
same-boundary connections, occupied source launch points, multi-output hubs with
fanout/total-length counterexamples, full source relay costs and type reselection,
hub routing fallback, short/long relay cost tradeoffs, occupied relay launch
positions, mixed-type chains in both type orders, partial final segments, huge
distance fallback, branch-type cost tradeoffs with fanout/total-length checks,
local multi-output merges with fanout/total-length and weighted-skew checks,
malformed input, and output preservation.
The supplied grading archive is only a read-only reference and is never executed.

Tester reports belong in `issues/opened/<NNN issue>/README.md`, with reproductions
alongside and dated replies in `comments/`. Coder fixes are committed for tester
review; only tester moves resolved issues to `issues/closed/`. Three-digit issue
numbers start at 001 and share one sequence across both states; closing preserves
the number and comment history. Quote issue paths in shell commands because the
directory names contain spaces.

## Repository contents

Track source, build configuration, tests, documentation, supplied examples, and
complete issue records (descriptions, comments, reproduction inputs, witness
outputs and verification helpers). Tester acceptance and moves to `issues/closed/`
are recorded in Git without changing their decisions. Shared collaboration
instructions and `.codex/agents/*.toml` role definitions are also tracked.

Generated builds, run directories, and experiments in `tmp/coder/` or
`tmp/tester/` are ignored, as are local editor state and desktop shortcuts.
Issue `.cbi` files and example figures remain tracked as reproducible evidence.

## Submission

Follow `Project2 Requirement.pdf` where packaging instructions differ from the
project handout: name the executable `cbi`, use three-digit input/output names,
and submit source as `StudentID.zip`. Exclude `build/`, `inputs/`, and `results/`.
Submit the required report separately as `StudentID.pdf` (at most three pages).
The current repository is the development baseline; final report and packaging
remain for the completed optimization stage.
