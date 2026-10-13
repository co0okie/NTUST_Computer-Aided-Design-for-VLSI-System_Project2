# SA + LP implementation and trade-offs

The solver provides `current`, `sa_lp`, and `hybrid`. The default is `hybrid`:
retain the established method and let SA + LP compete on exact legal Score.
No dominance assumption is made. This is a bounded heuristic, not a proof of
feasibility, global optimality, or annealing convergence.

## Organization and ownership

`src/core/` owns the model, safe arithmetic, structural checks and exact evaluation;
`src/io/` parses and serializes; `src/construction/` constructs legal seeds;
`src/search/` contains legacy refinement, placement/repair and annealing;
`src/solver/` schedules strategies. The executable only handles CLI and IO.
`src/tree.hpp` is a compatibility include for earlier reproduction helpers;
helpers now need to link `cbi_core` rather than compiling header definitions.

C++17 is retained. Nodes store an explicit `NodeKind` and `BufferTypeId`, and
structural traversals return `NodeId`s. Owned state uses values and standard
containers. The legacy builder/cache implementations are hidden behind move-only
or noncopyable RAII wrappers. No owning raw pointers, manual allocation, implicit
ID conversions, mutable globals, or shared ownership are introduced. New search
failures use `Expected<T>` and `ErrorKind`; legacy exception-based routines are
isolated behind checked entry points. The builder owns an immutable problem in
an address-stable context; trial copies borrow only that context. Relocation
caches own immutable snapshots, so temporary inputs or external edits cannot
create dangling/stale references. All score arithmetic is checked.

Candidate generation takes the state by value. Accepted candidates replace the
whole current state; rejected candidates never mutate it. Buffer deletion compacts
coordinates, capacities, directions and child indices together. Fixed source and
sink nodes retain their prefix positions in the tree.

## Placement model and repair

Capacity choices are separate from output buffer types. Each child-indexed edge
has a fixed quadrant, with non-strict axis inequalities to permit horizontal and
vertical edges. Fixed quadrants make Manhattan length linear. Variables represent
continuous buffer coordinates and node arrivals, plus minimum/maximum sink arrival.
The model constrains each driver's **summed outgoing length**, including SRC, and
arrival equalities. Buffers stay in `[1,dimX-1] × [1,dimY-1]`; pins never move.
Fanout/structure are checked outside the LP. Coordinate uniqueness is repaired.

A single-thread HiGHS simplex solve minimizes skew. A second solve, enabled by
default, minimizes wire length while preserving the optimal skew within `1e-7`.
It reduces gratuitous motion but can also remove geometries with better eventual
integer/type cost. `--wire-tiebreak 0` permits that trade-off to be measured.
The LP objective does not incorporate later cheapest-type normalization, so it
is only a guide to the final objective. Simplex is not claimed to have a
polynomial worst-case runtime.

Coordinates are translated by the minimum pin coordinate during modeling.
Dimensions, source capacity and selected buffer lengths exceeding `2^40` are
excluded from the LP path; the integer method remains available. LP placement is
also capped at 4096 nodes by default. These are search/numerical limits, not
statements that the original problem is infeasible. Exact checked integer
validation decides whether any candidate can be accepted.

Rounding is followed by collision detection and bounded greedy relocation.
The repair tries original coordinates and radius 1/2/4 neighborhoods, with 256
trials by default. It checks boundaries, directions, summed wire limits and
available library types. Output types are normalized transactionally to the
cheapest compatible choices; feasible search capacities remain intact. Actual
capacity upgrades are reflected in the repaired state.

An infeasible or unrepairable candidate may retry with maximum-length capacities
for its fanouts. A final attempt inserts one clustered buffer and repeats LP and
repair. There are at most three pipeline attempts, all sharing the deadline.
No illegal intermediate tree is accepted. The repair is deliberately finite and
can reject an LP solution even when another integer placement exists.

## Annealing and initialization

The seed builder first checks direct driving, then tries the existing binary
constructors with both parity modes, then falls back to the established complete
strategy. It never labels failure to find a seed as a proof of infeasibility.
`current` runs the existing solver first, preserving its ordinary completed result.
`hybrid` additionally retains a quick legal seed if the reference phase expires.

Moves are sink reparenting (25), sink swap (15), insertion (15), deletion (10),
direction change (20), capacity change (10), and small-subtree reparenting (5).
Insertion supports one through four clustered children: unary insertion is needed
for relay/delay topologies and makes unary deletion reversible. Subtrees initially
contain at most four sinks. Reparent/swap sampling uses nearest-five candidates
85% of the time and all structurally eligible candidates otherwise. Current
Manhattan distances rank candidates; they do not prove placement infeasibility.

Median estimates guide capacity selection. If no type satisfies estimated length,
a maximum-length fanout-compatible type is still tried. Child coordinates are
movable, so the estimate is not a hard feasibility filter. Capacity moves can
choose tighter or looser constraints independently of the final cheapest type.
Dominated/equal library capacities are excluded from capacity-move candidates,
while original indices/names and the existing solver's library remain intact.

Every acceptance uses exact repaired Score. Improvements/ties are accepted;
worse candidates use `exp(-delta/T)`. Auto temperature uses the median of the
first 24 positive candidate differences for an 80% initial acceptance target,
with a fallback of `max(1,0.02*initial_score)` until calibrated. Geometric cooling
reaches 1% of that temperature over the time or iteration budget. Repair and biased
sampling do not satisfy a demonstrated detailed-balance condition; this is an
optimization heuristic, not an equilibrium sampler.

## Budgets, observations and reproducibility

The default total soft limit is 55 seconds, including parsing and construction,
reserving approximately five seconds under the assignment's 60-second hard limit
for final evaluation, serialization and IO. It is not a hard real-time guarantee.
Legacy search checks deadlines while retaining completed candidates. A single LP
is allotted 0.2 seconds (its optional wire refinement has its own smaller remaining
slice), bounded by the global deadline. A timed-out solver candidate is discarded.

Hybrid gives the reference phase up to 40% of the overall budget; when that phase
finishes sooner, SA receives the remaining time immediately. Pure `sa_lp` gives
seed construction up to 40%, without ordinary reference refinement unless needed
as the seed fallback. If the incumbent attains the proven Score lower bound,
search stops. The default seed is 20261013.

Fixed-seed, fixed-iteration runs reproduce results on the same binary/platform
when LP/global timeouts do not intervene. Timed runs depend on machine load.
Statistics report attempts, feasible candidates, accepts, global improvements,
move timings, LP/repair timings and actual incumbent checkpoints. An LP call count
means one placement attempt, including its optional second wire solve; LP timing
includes model preparation, structural checking and solver execution. Rates and
average move timings can be derived from the raw counts.

`tools/benchmark.py` independently verifies every successful output, records
Score/skew/cost/buffers/time/peak RSS, and freezes the executable with a checksum
before starting work. It never imports or executes the grading code. Parallel
measurements share host load, so timing results are local evidence only.

## Verification

The structure-only and explicit-node-type changes passed Release/Debug tests and
149 differential inputs against the original binary: completion status, Score
and serialized outputs were identical. The new tests cover exact arithmetic,
cycles/repeated parents, source total wire, LP versus small integer enumeration,
unsafe domains, malformed capacity/direction vectors, collisions, bounded repair,
capacity normalization, all seven moves, rollback, fixed-seed iterations, deadlines,
CLI validation and output preservation.

ASan/UBSan tests run with `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
operate under this environment's ptrace restrictions. The bundled HiGHS code is
not sanitizer-instrumented. The grading archive was only read, never executed.
A local GCC 11 configure plus serial default build took 13.24 seconds, including
test executables.
Ubuntu 24.04 container compatibility was verified on 2026-10-13 using Podman,
GCC 13.3, two CPUs, 8 GiB RAM and disabled networking; bundled HiGHS linked
and all four project test suites passed. The actual grading VM remains untested.
See [environment validation](grader-environment.md).

Measured comparisons and parameter experiments are recorded in
[the experiment report](../benchmarks/sa_lp/README.md).
