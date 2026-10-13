# Pipeline ablation study — 2026-10-13

The study was conducted before the production cleanup, with solver sources,
strategies and defaults unchanged during the experiments. The clearest functional
duplicate on the tested corpus is repeated `final_delay`: removing all its calls
preserves the output byte-for-byte on **185 distinct current-strategy inputs**, including six
large new inputs. Delay insertion inside adaptive search is still active.

This is not a blanket recommendation to delete the other steps. Additional
experiments overturned two tempting conclusions: large-tree contraction matters
on every large holdout input, and the first initial-tree pass can protect Score
when the expanded construction pipeline runs out of its time slice. Five-second
hybrid confirmation also has mixed outcomes, including for `final_delay`.

The subsequent cleanup removes standalone `final_delay` passes and reuses a
full reference solve when initial construction already required it. Fast initial
candidates, adaptive delay insertion, all other search stages and defaults are
retained. Historical measurements below describe the pre-cleanup source.

Cleanup validation passed all four regression suites. Before/after outputs are
byte-identical and independently legal for 185 current-strategy inputs and eight
fixed-iteration SA/hybrid runs (100 iterations, seed 7), including the unary
fallback case and all six large holdout cases. A default serial clean build in
the same Ubuntu container took 25.06 seconds including configuration. See
[cleanup-validation.json](cleanup-validation.json). Fixed-time SA comparisons
above remain mixed; these checks do not establish equal Scores for every timed
annealing run.

## Experimental controls

Source commit: `0772bca`. `tools/ablation.py prepare` now copies this committed
revision by default (override with `--revision`) so the removed stages remain
available for reproducing the study. Experiments patch an isolated source copy; the
`CBI_STUDY_DISABLE` environment variable is never read by production code.
The instrumented source hashes and the executable SHA-256 are in
[instrumentation.json](instrumentation.json) and [metadata.json](metadata.json).

- Ubuntu 24.04.5, GCC 13.3, glibc 2.39; default CMake build, no Release flag.
- Network disabled, UID 1000, two CPU quota and CPU affinity, 8 GiB memory.
- One solver process at a time. Case/configuration/seed order is shuffled using
  fixed seed 20261013. All outputs are checked by an independent integer verifier.
- The all-enabled instrumented baseline matched 79 current-strategy outputs and
  six fixed-iteration SA/hybrid outputs from the production executable.
- Failures are recorded separately and are never treated as low Scores. Main
  comparisons use per-input wins/ties/losses and normalized deltas, rather than
  raw Score totals across inputs with different weights.
- Seeds are paired, but deleting a move changes the proposal distribution and
  RNG trajectory. Fixed-time annealing also uses elapsed time for cooling;
  identical seeds are not identical execution traces.

The experiment does not execute or import any grading program.

## Runs and datasets

| Phase | Inputs / configurations | Executions |
| --- | --- | ---: |
| Baseline equivalence | 79 current + 6 SA/hybrid comparisons | 85 |
| Legacy leave-one-out | 79 inputs, baseline + 12 omissions | 1,027 |
| SA screening | 44 training inputs, 19 configurations, 3 seeds | 2,508 |
| Legacy holdout | 100 new random + 6 large inputs, 4 configurations | 424 |
| Timed confirmation | 6 inputs, 5 configurations, 3 seeds | 90 |
| Initialization stress | 4 sizes, 4 time budgets, 3 configurations | 48 |
| Total | | 4,182 |

4,162 runs produced independently legal outputs. The 20 failures consist of
seven intentional legacy omissions exposing missing fallback functionality, and
13 short-budget stress failures (including eight baseline failures). There were
no illegal/failed SA screening or timed-confirmation outputs.

The initial corpus is the supplied example, 48 issue inputs and 30 generated
cases. SA screening excludes current results attaining the proven Score bound,
instances above 128 sinks, and geometry outside the LP numerical domain.
Generated IDs 015–029 are reserved from screening. This leaves 44 training
inputs; see [screen-cases.txt](screen-cases.txt).

The legacy holdout has 100 new generated inputs, seed 20261014, plus six cases
with 600/1000 sinks, dimensions 100×100, SRC fanout/length 2/20, and buffer types
4/20/cost 1 and 8/64/cost 3. These deliberately create large trees whose generic
bound is not tight. Inputs are in [the holdout corpus](../ablation-holdout/inputs/).

## Legacy leave-one-out

Current strategy is deterministic. Every configuration gets a 25-second safety
cap. Most finish much earlier. Better/worse/equal compares against the complete
baseline on the same input; failure is separate.

| Removed step | Legal / 79 | Better | Worse | Equal | Failed | Median time ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Adaptive refinement | 79 | 0 | 11 | 68 | 0 | 0.821 |
| GCD-compressed relay planner | 79 | 0 | 2 | 77 | 0 | 0.946 |
| Dense forest fallback | 76 | 0 | 1 | 75 | 3 | 0.874 |
| Repeated final_delay passes | 79 | 0 | 0 | 79 | 0 | 0.936 |
| Joint bypass and child relocation | 79 | 0 | 7 | 72 | 0 | 0.877 |
| Joint hub membership/position | 79 | 0 | 7 | 72 | 0 | 0.981 |
| Large-tree contraction | 79 | 0 | 0 | 79 | 0 | 1.000 |
| Layered proximity construction | 79 | 0 | 4 | 75 | 0 | 0.945 |
| Legacy local-search operators | 79 | 1 | 32 | 46 | 0 | 0.592 |
| Fanout-aware multiway construction | 79 | 0 | 5 | 74 | 0 | 0.792 |
| Protected reference construction/refinement | 76 | 0 | 30 | 46 | 3 | 0.526 |
| Small-forest fallback | 78 | 0 | 0 | 78 | 1 | 0.884 |

Dense forest, small forest and reference construction have direct feasibility
value. Removing local search speeds execution but worsens 32/79 inputs. Joint
moves, adaptive refinement and the newer construction families each improve
some inputs and cannot be dismissed as unused machinery.

### Holdout overturns an apparent redundancy

| Removed step | Legal / 106 | Better | Worse | Equal | Median time ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| no_final_delay | 106 | 0 | 0 | 106 | 0.934 |
| no_large_contract | 106 | 0 | 6 | 100 | 1.005 |
| no_tail | 106 | 0 | 6 | 100 | 0.952 |

Removing large-tree contraction worsened all six large cases. For example,
large005 went from Score 1139 to 1331. Its apparent absence of benefit on the
original 79 inputs was a coverage problem, not evidence that the method was
redundant.

All 79 original and 106 new `no_final_delay` outputs are byte-identical to their
baselines, not merely equal in Score. The paired median current-strategy time
ratio is about 0.934 in both corpora. See
[the 185 comparisons](final-delay-equivalence.json). This supports investigating
whether adaptive delay searches have superseded these repeated wrapper passes;
it does not show that delay insertion itself should be removed.

## SA/LP screening

Every configuration uses 100 attempted discrete moves, seeds 1, 7, 17, and a
25-second safety cap. Stops at the proven bound remain allowed. There were no
deadline-censored screening runs. Omitting SA intentionally attempts no moves.

`no_capacity_retry` keeps only the first placement attempt, removing both
capacity escalation and the optional final structural-repair attempt.
`no_structural_repair` retains the first two placement/capacity attempts.
The `lp_seconds` counter measures the placement-evaluation block; in `no_lp`
it includes creating the existing-coordinate proxy, not a HiGHS solve.

The first three columns compare per-input three-seed medians; the next two count
individual paired-seed improvements/regressions. A zero median-loss count can
still hide individual losses.

| Omission | Cases better | Cases worse | Cases equal | Seed runs better | Seed runs worse | Median time ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| greedy | 3 | 2 | 39 | 9 | 8 | 0.930 |
| no_capacity_retry | 2 | 0 | 42 | 4 | 2 | 0.748 |
| no_change_direction | 3 | 1 | 40 | 7 | 5 | 0.939 |
| no_change_type | 2 | 3 | 39 | 5 | 10 | 1.013 |
| no_delete_buffer | 1 | 6 | 37 | 6 | 13 | 1.088 |
| no_initial_pass | 0 | 0 | 44 | 0 | 0 | 0.987 |
| no_insert_buffer | 2 | 3 | 39 | 6 | 9 | 0.804 |
| no_integer_repair | 4 | 1 | 39 | 9 | 5 | 1.115 |
| no_locality | 0 | 3 | 41 | 6 | 6 | 0.971 |
| no_lp | 0 | 6 | 38 | 2 | 14 | 0.554 |
| no_move_sink | 1 | 2 | 41 | 10 | 10 | 1.080 |
| no_move_subtree | 0 | 5 | 39 | 4 | 10 | 0.985 |
| no_reference_pass | 0 | 16 | 28 | 3 | 45 | 0.819 |
| no_sa | 0 | 7 | 37 | 0 | 17 | 0.224 |
| no_seed_lp | 0 | 0 | 44 | 0 | 0 | 0.980 |
| no_structural_repair | 2 | 0 | 42 | 4 | 1 | 0.817 |
| no_swap_sinks | 0 | 5 | 39 | 5 | 12 | 0.885 |
| no_wire_lp | 4 | 3 | 37 | 12 | 8 | 0.804 |

The LP omission retains the existing geometry as the placement candidate,
capacity retries, rounding/repair and exact validation. It therefore measures
loss of LP placement, not removal of validity checks. `no_integer_repair` keeps
rounding and validation but sets neighborhood-repair trials to zero. `greedy`
rejects strictly uphill moves while still accepting equal-Score moves.
`no_reference_pass` skips the explicit comparison against the complete legacy
solver; initial construction can still fall back to it when necessary for a
legal seed. It is not a completely legacy-free method.

LP removal worsens six case medians; SA removal worsens seven; removing the
reference comparison worsens sixteen. Deletion, swaps and subtree reparenting
also have direct value. Wire-minimization LP, structural repair, integer repair,
locality and uphill acceptance show tradeoffs, not universal dominance. In
particular, removing retries or structural repair has no median case losses but
still regresses two/one individual seed runs respectively.

The strongest initialization candidates in this short fixed-work screen were
`no_initial_pass` and `no_seed_lp`: all 132 paired Scores equal baseline for
both. Their median time savings are only approximately 1.3% and 2.0%.

## Fixed-time confirmation

The confirmation corpus was specified before examining its ablation results:
the example, issue 019, and evenly spaced held-out generated IDs 015, 019, 023,
027. Every method gets five seconds and seeds 1, 7, 17. Individual omissions and
`lean_initialization` (both omissions together) are tested; `no_final_delay` was
added after its deterministic holdout checks.

| Omission | Legal / 18 | Cases better | Cases worse | Cases equal | Seed runs better | Seed runs worse |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lean_initialization | 18 | 0 | 2 | 4 | 0 | 3 |
| no_final_delay | 18 | 1 | 2 | 3 | 2 | 5 |
| no_initial_pass | 18 | 0 | 3 | 3 | 1 | 5 |
| no_seed_lp | 18 | 0 | 2 | 4 | 2 | 4 |

These results do not establish a safe across-the-board deletion of any of the
initialization candidates. `final_delay` remains a strong functional duplicate
on the deterministic corpus, but its timed hybrid outcomes are mixed. Fixed-time
cooling, iteration throughput and a small three-seed sample can contribute to
these differences. They should not be interpreted as proof of intrinsic benefit
from an otherwise unchanged initial tree, nor ignored when changing defaults.
More fixed-work/seed evidence is appropriate before claiming global dominance.

## Short-budget initialization stress

The stress inputs use 256/512/1024/2048 sinks over a 1,000,000-square domain, SRC
1/20, and three competing types: cheap long binary 2/1,000,000/cost 1, short
3/64/cost 2, and expensive high-fanout 64/1024/cost 1000. Seeds are 20261015+i.
Inputs are in [the stress corpus](../ablation-stress/inputs/). The same hybrid
strategies get 0.25, 0.5, 1 and 2 seconds, seed 7.

Baseline was legal on 8/16 input-budget pairs; no-initial-pass was legal on
13/16, and the combined omission on 14/16. Thus the initial pass also consumes
precious construction time at extremely short deadlines. However, among the
eight baseline-legal pairs, both omissions improve one, tie two and worsen five.
At input001 / 0.5 seconds, baseline Score is 97,813 while removing the initial
pass yields 79,617,219. The quick candidate can protect quality when the expanded
construction pipeline cannot reach its reference strategy in time. Omitting the
pass is not universally better merely because longer complete runs agree.

## Concrete simplification directions

1. Investigate repeated `final_delay` calls first, keeping adaptive delay
   insertion. This is the best deterministic redundancy evidence, with timed
   hybrid tradeoffs explicitly unresolved.
2. Reuse an initial result when initial construction already fell back to the
   full legacy solver, instead of immediately running that solver again. This
   is a narrower code-level duplicate than removing all initial construction.
   In issue 001, direct driving is illegal and unary buffers cannot form the
   basic binary tree, so `initial_solution` calls `solve`; the hybrid caller
   then calls `solve` again. Checkpoints show equal Scores before/after the
   second pass (e.g. 2 at 0.0065 seconds and 2 at 0.0125 seconds). This finding
   follows from code inspection and traces, rather than a separately measured
   cached-result implementation.
3. Prefer cheap impossibility checks over deleting whole SA operations. With
   one eligible buffer type, type changes have no alternative; with every free
   interior position occupied, inserting another buffer cannot be legal.
   Dense issue 030 already consumes its three free positions. These are
   candidates for conditional rejection before LP, not tested production changes.
4. Consider tighter proven early-stop bounds for already optimal relay chains.
   For one sink, all legal buffers form a chain and Score equals buffer cost.
   Coverage DP can provide a stronger optimistic cost bound on
   `distance(SRC,sink)-SRC.length`; a validated tree matching it needs no SA.
   This is a mathematical follow-up idea, not an implemented or timed ablation.

None of the construction/fallback families is recommended for blanket removal.
Production algorithms were not modified based on incomplete evidence.

## Artifacts and reproduction

- [runs.csv](runs.csv): every run, including failures, Score, timing, LP/repair
  counts, termination and attempts.
- [moves.csv](moves.csv): move-level counters for screening, confirmation and stress.
- [summary.csv](summary.csv), [summary.json](summary.json): paired summaries.
- [metadata.json](metadata.json): executable/input hashes, seeds and plans.
- [instrumentation.json](instrumentation.json): exact source-hook hashes and variants.
- [confirmation-plan.json](confirmation-plan.json): held-out confirmation selection.

Raw outputs, statistics and logs are retained in ignored storage under
`tmp/coder/ablation-20261013-v2/project/study-results/`.

```sh
python3 tools/ablation.py prepare --work tmp/coder/ablation-replay
podman run --rm --network none --cpus 2 --memory 8g --memory-swap 8g \
  --userns keep-id --user 1000:1000 \
  -v "$PWD/tmp/coder/ablation-replay/project:/home/grader/M00000000:Z" \
  localhost/cbi-grader-ubuntu24:20261013 bash
```

Inside the container, build the copied sources, then run phases sequentially:

```sh
cmake -S /home/grader/M00000000 -B /home/grader/M00000000/build
cmake --build /home/grader/M00000000/build
cd /home/grader/M00000000
python3 tools/ablation.py legacy --output study-results
python3 tools/ablation.py screen --output study-results
python3 tools/ablation.py legacy_holdout --output study-results
cp benchmarks/ablation/confirmation-plan.json study-results/confirmation-plan.json
python3 tools/ablation.py confirm --output study-results
python3 tools/ablation.py init_stress --output study-results
python3 tools/ablation.py export --output study-results
python3 tools/summarize_ablation.py study-results
```

The independent `verify` phase additionally requires mounting an uninstrumented
baseline at `/home/grader/normal-cbi`. The image preparation recipe and grading-
environment limitations are described in [environment validation](../../docs/grader-environment.md).
Cached runs are resumed only for the same executable and controls. Timed replay
will vary with machine/load; the artifacts above identify this exact experiment.
