# Retry and LP tuning — 2026-10-13

This study uses the post-cleanup solver at source commit `4552689`. Experiments
use isolated source copies with baseline defaults; subsequent production changes
do not alter the measured executable. `tools/tuning.py`
pins the original production source (`--revision 4552689` by default) in an
isolated copy with retry-level statistics and experimental
policies; production never reads the experiment environment variables.

## Protocol

The protocol and corpus split were fixed before running the experiments; see
[protocol.json](protocol.json). Eight training inputs, five seeds
(1, 7, 17, 31, 101), and ten variants give 400 screening runs. Each gets 100
attempted SA moves and a 25-second safety budget. Reaching the proven lower bound
can stop a run early. The reference phase still runs, so this is a hybrid study,
not a comparison of identical initial trees.

The best non-baseline variant is selected using the mean of per-input five-seed
median relative Score differences, then fewer median regressions and faster
median elapsed time. A non-improving selection is exploratory and does not
justify changing defaults. Four reserved generated inputs then compare the
baseline and selected challenger with five seeds and 25 seconds per run (40
confirmation runs). This split is held out from this tuning round; some inputs
were used by the earlier ablation study.

Every output is independently verified for exact integer geometry, topology,
fanout, summed outgoing wire length, arrivals, cost and Score. Failures are
recorded separately, never treated as good Scores. The environment is Ubuntu
24.04/GCC13, a default serial CMake build, 2 CPU quota plus affinity, 8 GiB memory,
no network, and one solver at a time. Each subprocess has an external 30-second
limit. Compilation and solving are separate measurements. Container results do
not establish grader VM timing guarantees.

The all-enabled instrumented baseline must preserve production output before
screening. [baseline-verification.json](baseline-verification.json) records 24
byte-identical fixed-iteration comparisons: 12 inputs × SA/hybrid, seed 7.
All outputs, including the 24 unique-retry comparisons, are legal.

## Variants

| Variant | Change |
| --- | --- |
| baseline | Current defaults |
| retry_one | Only the first LP/repair attempt |
| retry_two | Retain capacity escalation; omit final structural repair |
| retry_unique | Skip the second attempt when capacity IDs are unchanged; retain the third attempt |
| wire_off | Omit secondary wire minimization |
| dual_objective | Evaluate both secondary-wire and skew-only placements, repair each, choose lower exact Score |
| lp_005 / lp_010 | Per-LP allowance 0.05 / 0.1 seconds, versus 0.2 |
| reference_020 / reference_060 | Reference deadline fraction 0.2 / 0.6, versus 0.4 |

The reference fraction is a cap, not mandatory time spent. If reference search
finishes before every tested cap, changing it has no effect on the available SA
time except timing noise. A per-LP limit can likewise be inactive when solves
finish sooner. The dual-objective prototype changes candidate selection and can
spend up to twice the evaluation work; fixed-work gains need timed confirmation.

Retry attempt numbers 1/2/3 denote initial placement, maximum-length capacity
retry, and clustered-buffer structural repair. `attempts` counts attempts that
reach LP. `successful` counts completed legal placements, including ones later
rejected by SA. `improvements` counts accepted new SA best Scores, excluding seed
refinement; for the dual variant, credit belongs to the chosen placement's
attempt. `seconds` covers per-attempt capacity escalation, LP and integer repair;
initial capacity estimation is outside these timers. Attempts stopped before LP
can consume time without increasing `attempts`.

## Fixed-iteration screening results

All 400 runs are legal; no run reaches its time deadline. The following counts
compare per-input five-seed median Scores. Time ratios are medians of paired-seed
elapsed-time ratios, including reference construction; small ratios can contain
measurement noise.

| Variant | Better / equal / worse inputs | Better / equal / worse seeds | Mean case-relative Score delta | Median time ratio |
| --- | --- | --- | ---: | ---: |
| baseline | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 1.000 |
| dual_objective | 3 / 5 / 0 | 8 / 28 / 4 | -3.18% | 1.420 |
| lp_005 | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 0.947 |
| lp_010 | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 0.990 |
| reference_020 | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 0.998 |
| reference_060 | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 0.912 |
| retry_one | 1 / 7 / 0 | 2 / 36 / 2 | -0.90% | 0.823 |
| retry_two | 0 / 8 / 0 | 1 / 38 / 1 | +0.00% | 0.839 |
| retry_unique | 0 / 8 / 0 | 0 / 40 / 0 | +0.00% | 0.914 |
| wire_off | 2 / 5 / 1 | 7 / 26 / 7 | +2.94% | 0.795 |

The predeclared selection rule chooses `dual_objective`. Its three improving
case medians and approximately 3.18% mean relative improvement cost approximately
42% more elapsed time per paired fixed-work run. Four individual seed runs
regress. Timed confirmation is necessary before promoting it.

`retry_unique` removes 346 of 4,467 baseline LP calls (7.75%) and preserves all
40 screening output trees byte-for-byte, in addition to the 24 independent
pre-screen comparisons. This supports the narrow production change that skips
an identical capacity retry while retaining structural repair. Entire retry
stages remain useful search options; a successfully repaired non-best candidate
can still influence later SA moves.

Both LP limits and both reference fractions preserve all 40 output trees and
LP-call counts. Every baseline reference phase finishes within 1.09 seconds,
well before even the shortest tested five-second reference cap. These inputs
do not exercise the time-allocation tradeoff. Differences in their measured
elapsed time do not establish a benefit from changing those limits.

For baseline screening, attempt 1/2/3 reaches LP 3,370/581/516 times, produces
2,789/65/84 legal candidates, and consumes 21.91/2.06/2.10 seconds inside the
attempt timers. All 20 newly accepted SA best Scores are credited to attempt 1.
Zero directly credited improvements does not establish that the other attempts
are unnecessary: accepted intermediate states and altered trajectories matter.

## Fixed-time confirmation results

All 40 confirmation runs produce independently legal output within 25.02 seconds
of subprocess wall time. The challenger improves one case median, ties two and
regresses one; across 20 paired seeds it wins four, ties eleven and loses five.
The mean of per-case median relative Score deltas is **+2.10%** (worse).

| Input | Baseline median (min–max) | Dual-objective median (min–max) |
| --- | ---: | ---: |
| input015 | 39 (37–43) | 43 (38–43) |
| input019 | 7 (7–7) | 7 (7–7) |
| input023 | 107 (105–107) | 105 (105–107) |
| input027 | 13 (11–17) | 13 (11–14) |

Fixed-work placement gains do not translate to a reliable timed improvement on
this reserved corpus. The dual-objective method stays in the isolated experiment
instead of becoming a production default. A possible follow-up is evaluating a
second placement only for expensive or poorly repaired candidates; this study
does not test that policy. Wire minimization, LP time limits, reference fraction
and whole retry-stage defaults are retained.

## Production decision

Promote only `retry_unique`: after capacity relaxation, compare the complete
capacity-ID snapshot and omit the second placement/repair when it is identical.
The third structural attempt remains available. The implementation keeps owned
value snapshots, `Expected`/`Error` results, exact validation and current defaults;
no experiment environment variables enter production.

The uninstrumented production candidate passes all four regression suites and
120 before/after comparisons (12 inputs × five seeds × SA/hybrid, 100 iterations,
25-second safety budget). Every output tree and stdout is byte-identical and
independently legal. Aggregate LP calls fall from 13,263 to 12,576 (**5.18%**).
A default serial clean build takes 27.50 seconds including configuration.
See [production-validation.json](production-validation.json) and
[production-equivalence.json](production-equivalence.json). Fixed-time SA can
follow a different trajectory after skipping work; this does not establish equal
or improved Scores for every timed run.

Do not promote the dual-objective prototype or globally remove a retry stage.
Keep the LP time limit and reference fraction until larger inputs actually
exercise their caps. Further experiments should try selective second-objective
placement, problem-focused SA proposals and restarting from different legal
seeds under the same total budget.

## Interpretation limits

Lower Score is better. Report paired-seed outcomes and per-input seed medians;
use relative deltas so different input scales do not dominate the comparison.
Seeds are paired but changes in candidate acceptance alter the search trajectory.
Fixed-time cooling also depends on evaluation speed and instrumentation overhead.
Five seeds and four confirmation inputs are exploratory evidence, not proof of
universal dominance or statistical significance. Particularly small timing gains
require a repeat using an uninstrumented production candidate.

## Reproduction

```sh
python3 tools/tuning.py prepare --work tmp/coder/tuning-repeat
cmake -S tmp/coder/tuning-repeat/project -B tmp/coder/tuning-repeat/project/build
cmake --build tmp/coder/tuning-repeat/project/build
python3 tmp/coder/tuning-repeat/project/tools/tuning.py verify \
  --root tmp/coder/tuning-repeat/project --output tmp/coder/tuning-repeat/verification \
  --baseline build/cbi
python3 tmp/coder/tuning-repeat/project/tools/tuning.py screen \
  --root tmp/coder/tuning-repeat/project --output tmp/coder/tuning-repeat/project/tuning-results
python3 tmp/coder/tuning-repeat/project/tools/tuning.py confirm \
  --root tmp/coder/tuning-repeat/project --output tmp/coder/tuning-repeat/project/tuning-results
```

Use the documented container constraints for comparable timing. Exported CSV
files use LF line endings. Raw run JSON, output trees, build logs and progress
logs live in ignored `tmp/coder/`; metadata records executable and input hashes.
