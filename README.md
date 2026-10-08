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

For inputs with at most 128 sinks, an additional binary candidate searches
branch outgoing-budget splits. A range of at most 65 integer splits is enumerated;
larger ranges sample coverage jumps near both ends and the middle for at most
eight library types. Coverage estimates rank candidates, then each branch tries
up to eight actual routes in isolated state, with at most 128 routing trials per
Builder and 512 components per trial. Complete routing reselects buffer types;
only shortest branch paths are adopted so subtree arrival offsets stay valid.
Failed/blocked/detouring trials retain ordinary routing. No search scales linearly
with a large length limit. The original equal-budget candidates and their local
improvement results remain available. Up to four allocation trees form a separate
shortlist with 2048 shared local trials, bringing the total local-improvement cap
to 6144 without displacing the original candidate shortlist. Final comparison
uses fully validated weighted Score. These bounds leave some allocations unexplored.

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
The constructor also retains up to four distinct trees of at most 512 components,
ranked by initial Score and then deterministically by stored geometry/topology.
The original winning tree keeps its full local-search budget; other shortlisted
trees share at most 2048 additional trials. Their improved, fully validated Scores
compete with that result, so exploring ties cannot worsen the previous winner.
Exact duplicate stored layouts are skipped. The shortlist is bounded and may
still omit a topology with greater improvement potential.

After preserving the winning tree's full refinement, at most six extra
representatives receive complete refinement: the lowest initial Score and cost
from general/binary, star and hub families. This lets a topology with initially
poor skew compete after geometry and membership changes. Exact stored duplicates
are skipped. These alternatives run only for at most 128 sinks and 512 components,
share 32768 additional trials and 16 accepted delay insertions, and receive at
most 8192 trials each, including bypass. Phase/call limits still apply; only a
strictly lower validated final Score replaces the established result. Finite
representatives and shared budgets can still omit promising constructions.

Trees larger than 512 components receive a separate bottom-up contraction pass,
without enabling expensive geometry or membership searches. A child buffer is
bypassed only when every grandchild's direct Manhattan edge equals the old two
edges' combined length. This preserves all sink arrivals and skew exactly.
The parent must have a compatible type and summed outgoing length, and its new
cost must be strictly less than the old parent and removed buffer costs. SRC keeps
its original limits. Only existing coordinates remain, so contraction cannot
introduce overlaps. A single copied tree accumulates changes; IDs are compacted,
types reselected and the complete result validated once before final comparison.
This pass handles at most 100000 components, 64 library types and degree 64,
with at most 1048576 contraction work steps counting edge/type examinations.
Traversal/compaction use linear space and do not copy/validate the tree per trial.
Triangle equality and one bottom-up sweep deliberately exclude other improvements.

Small trees also try synchronous exchanges of child roots between different
parents, with at least one buffer root. Roots that are ancestors of each other
are rejected before copying; the parents may themselves have an ancestor relation.
Coordinates and child counts remain fixed, but depths, arrivals and compatible
buffer types can change. Every trial reselects types and fully validates actual
weighted Score. The shared exchange core retains the existing leaf-only behavior.
Subtree exchanges handle at most 128 sinks/512 components, eight passes and 2048
scaled trials per call; even rejected ancestor trials consume the budget.
After retaining all prior refinement phases, up to three extra rounds of subtree
exchange, bypass, relocation, leaf exchange and leaf reparenting share 8192 trials.
Alternative construction candidates still use their existing global/slice limits.

After choosing the improved complete tree, a separate relocation pass adjusts
buffer positions while preserving topology. It samples radius-two integer offsets,
alignments with parent/children coordinates, and midpoint positions, with at most
32 positions per buffer. Pins and SRC stay fixed. Each trial reselects compatible
types and validates the entire tree, including the changed incoming edge and all
sink arrivals; only strict weighted Score improvements are accepted. The pass
handles at most 512 components, up to eight rounds and 2048 total evaluations,
with the same tree/library-scaled budget as bypass search. Earlier search budgets
remain intact; the first-pass cap including relocation is 8192 local evaluations.
Large dimensions do not increase the sampled neighborhood. This bounded pass
may stop before finding further valid moves or require moves it does not sample.

A final leaf-exchange pass tries swapping two sinks' parent edges. It keeps all
pin/buffer coordinates and component IDs fixed, reselects compatible types, and
validates complete arrival times, driving limits and weighted Score. Leaf-only
swaps preserve a connected acyclic tree, including when the parents have different
depths; output levels are regenerated. Same-parent pairs are skipped. The pass
handles at most 128 sinks and 512 components, with up to eight rounds and 2048
total evaluations, further reduced by tree/library size. Earlier search budgets
remain intact, for a first-pass cap of 10240 local evaluations. Only strict improvements
replace the result; bounded enumeration can miss beneficial exchanges.

After preserving that complete first-pass result, refinement revisits bypass,
relocation and leaf exchange in that order for up to three additional rounds.
These rounds share 6144 extra evaluations; each individual call still obeys its
original tree/library-scaled limit and component/sink cutoffs. All three moves
consume the same remaining counter, including rejected trials. Refinement stops
when an entire round does not reduce Score or the shared budget is exhausted.
Only strict, fully validated improvements replace the result. This refinement
sequence has a cap of 16384, retaining all original search budgets.
Later geometry or leaf changes can therefore unlock earlier buffer contractions,
though bounded rounds do not prove convergence or global optimality.

Leaf reparenting then moves one sink to another existing buffer or SRC, using
spare fanout and wire budget without requiring a reverse exchange. The destination
may receive a different compatible buffer type; SRC always keeps its input limits.
Removing an existing parent's last child is rejected, so empty buffers are never
left behind. Each trial preserves pin/component IDs and validates full weighted
Score, including all incoming arrival changes and actual source/parent wire totals.
The move handles at most 128 sinks and 512 components, up to eight passes and 2048
evaluations per call, further limited by tree/library size and a shared counter.
After retaining the established refinement result, up to three more rounds run
reparenting, bypass, relocation and exchange with 8192 shared extra evaluations.
They stop on no improvement or budget exhaustion; the cap through reparenting is
24576. Output levels are regenerated for depth-changing moves. This preserves
prior search budgets but remains a bounded heuristic with excluded last-child moves.

Delay-relay insertion can increase wire length on an early leaf or subtree edge
without changing intrinsic buffer delay (which is zero). It samples small endpoint
neighborhoods and bounding-box excursions derived from the actual arrival gap;
the floor/ceiling half-gap samples respect integer Manhattan parity. At most 32
positions per edge are considered, avoiding grid/delay-sized scans. Each trial
replaces one edge with a unary relay, reselects legal types and validates the full
weighted Score and all parent outgoing totals. Expensive or illegal detours keep
the original result; overflowing candidate distances are rejected.
After retaining previous refinement budgets, up to three extra rounds of delay,
reparenting, bypass, relocation and exchange share 10240 extra evaluations and
at most 16 accepted relay insertions. Delay calls have at most eight passes and
2048 trials, further reduced by tree/library size, with at most 128 sinks and
512 resulting components. The local-search cap through this phase is 34816 evaluations. This
bounded, strictly improving search can miss moves requiring several simultaneous
insertions or unsampled detour positions.

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
If all existing builders fail, a small-forest fallback tries alternative pairs
and merge positions in isolated states, allowing SRC to drive multiple groups.
It accepts only fully validated trees and runs only for at most eight sinks and
64 library types. Each merge samples at most 54 positions near its endpoints,
corners, midpoint and SRC, without scanning the chip. At most 64 states survive
each merge layer, trees contain at most 16 components, and all placement and
completion trials share a 65,536-attempt budget. It uses direct legal buffer
edges and may miss solutions requiring relay chains or discarded beam states.
Existing successful constructions and their optimization budgets are preserved.
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
equal-score construction alternatives that improve differently,
branch-budget allocation with swapped children, occupied launches, short edges
and huge capacities, fixed-topology relocation with reordered sinks, occupied
targets, boundary pins and huge dimensions, cross-branch leaf exchanges,
illegal source-budget exchanges and the large-input exchange cutoff,
merge revisits after later improvements and the 512-component refinement cutoff,
single-leaf reparenting with remaining capacity and reordered sinks,
delay relays with even/mixed parity, cost/capacity/boundary and large-integer checks,
small-forest fallback with reordered sinks,
full-refinement alternatives with reordered sinks, different weights and costs,
large-tree multi-output contractions with reordered sinks,
subtree/sink exchanges with reordered sinks and buffer type changes,
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
