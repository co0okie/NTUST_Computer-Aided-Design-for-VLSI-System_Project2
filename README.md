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
paths. Each parent obeys both its fanout limit and the **sum** of outgoing wire
lengths. After routing, each buffer receives the cheapest compatible library type.

Two merge strategies and a direct SRC-to-sinks tree are compared using the actual
objective `w_skew * (T_max - T_min) + buffer_cost`. Every candidate is validated
for unique coordinates, fixed pins, interior buffers, connectivity, tree
structure, and driving limits. Output levels are generated from tree depth.

All integer Manhattan paths between fixed endpoints have the same length parity.
Consequently, sinks with mixed `(x+y) mod 2` cannot have exact zero skew. One
strategy balances virtual sink arrivals with parity offsets to target skew 0
for uniform parity and skew 1 for mixed parity. This is a heuristic: merge
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
40 deterministic random sparse inputs, malformed input, and output preservation.
The supplied grading archive is only a read-only reference and is never executed.

Tester reports belong in `issues/opened/<issue>/README.md`, with reproductions
alongside and dated replies in `comments/`. Coder fixes are committed for tester
review; only tester moves resolved issues to `issues/closed/`.

## Submission

Follow `Project2 Requirement.pdf` where packaging instructions differ from the
project handout: name the executable `cbi`, use three-digit input/output names,
and submit source as `StudentID.zip`. Exclude `build/`, `inputs/`, and `results/`.
Submit the required report separately as `StudentID.pdf` (at most three pages).
The current repository is the development baseline; final report and packaging
remain for the completed optimization stage.
