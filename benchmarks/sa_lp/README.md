# SA + LP comparison — 2026-10-13

Both algorithms are retained. The default hybrid preserves a legal result from
the established method before using SA + LP. Pure SA + LP has wins and losses:
neither method has a demonstrated general dominance relationship.

## Recorded experiments

All 1,136 recorded executions produced legal outputs under the independent
integer verifier in `tools/benchmark.py`. The grading program was never run.
The binaries are frozen per experiment and identified by SHA-256 in
[summary.json](summary.json). Runs used three concurrent processes on the shared
Rocky Linux x86-64 development host; timing is workload/machine dependent.

The 1-second suite contains 79 cases: the supplied example, 48 issue inputs and
30 random inputs generated with seed 20261013. Random methods use seeds 1–5;
`current` is deterministic and is run once per case. There are 869 executions.
Compare each method's five-seed median against the current method:

| Method | Better cases | Equal cases | Worse cases |
| --- | ---: | ---: | ---: |
| Pure SA + LP | 11 | 43 | 25 |
| Hybrid | 14 | 65 | 0 |

Across individual seeds, pure SA + LP improved 61/395 runs and regressed 120/395;
hybrid improved 76/395 and regressed zero. Current completed these cases within
0.075 seconds, so its 40% hybrid slice did not truncate reference work. This does
**not** prove that a fixed 40% slice preserves the full reference result on harder
inputs. Candidate acceptance guarantees legality and retention of the incumbent,
not dominance over another strategy with a different time allocation.

The 55-second suite focuses on three non-optimal cases chosen from exploratory
results. It has 33 executions, with five seeds for each random method. Final
Score medians and ranges are:

| Case | Current | Pure SA + LP | Hybrid |
| --- | ---: | ---: | ---: |
| Supplied example | 41 | 37 (37–37) | 37 (37–37) |
| Random input024 | 64 | 50 (50–53) | 51 (50–53) |
| Issue 019 input001 | 105 | 63 (63–69) | 63 (63–70) |

These cases demonstrate further improvements within the assignment budget; their
selection does not make them a representative estimate of general improvement.
The actual 1/5/15/30/55-second incumbent observations are in [curves.csv](curves.csv).
Maximum measured process time across all runs was 55.111 seconds, and peak RSS
was 27,588 KiB. Local configure plus serial default compilation took approximately
13.24 seconds, including test binaries. Subsequent Ubuntu 24.04 container validation used Podman and passed all four
project test suites, including HiGHS LP execution. See
[environment validation](../../docs/grader-environment.md) for the separate
compatibility run; the measurements above remain the original Rocky Linux runs.

## Parameter experiments

A separate sweep used eight training cases (random IDs 000, 003, 007, 008, 009,
010, 011, 012), three seeds and a one-second hybrid budget: 144 runs. The same
legal reference result is protected in every configuration. The aggregate Score
sums below describe these exact inputs; they are not normalized across instances:

| Configuration | Sum of 24 Scores |
| --- | ---: |
| Default | 2009 |
| Global sampling only | 2002 |
| Local sampling only | 1989 |
| More deletion moves | 1961 |
| Repair budget 64 | 2003 |
| Disable wire tie-break LP | 1989 |

The deletion profile uses weights `20,15,10,25,15,10,5`, in the documented move
order. It was then compared with default on 15 held-out random cases (015–029),
three seeds each: 90 runs. Median Score improved on five cases, tied on nine and
regressed on one. Aggregate Score decreased from 3061 to 3022, but input024's
median worsened from 57 to 60. We therefore retain the original move distribution
as the default and expose the alternative through `--move-weights`. More favorable
training totals alone do not justify universal replacement.

LP modeling/solving dominated the first comparison: about 91% of combined LP/repair time
for pure SA + LP and 92% for hybrid. Disabling the second wire-minimization solve
increased the training suite's placement LP attempt count from 31,652 to 43,681, but had a
smaller quality benefit than the deletion profile. Its optimum LP skew need not
produce the cheapest integer geometry. The implementation keeps wire tie-break
configurable, and does not claim it always improves results. Incremental models,
basis reuse and adaptive move policies are deferred until a controlled experiment
justifies their complexity.

## Artifacts and reproduction

- [runs.csv](runs.csv): per-run legality, Score, skew, cost, buffers, time and RSS.
- [moves.csv](moves.csv): short-suite move counts, rates and average time.
- [curves.csv](curves.csv): long-suite actual incumbent checkpoints.
- [summary.json](summary.json): aggregated comparisons, configurations and binary hashes.
- [inputs/](inputs/): the 30 fixed generated cases.

`feasible_rate`, `accepted_per_attempt` and `improvement_rate` divide by attempts;
`acceptance_rate` divides by feasible candidates. Improvements count new global
best solutions, rather than all downhill moves. LP refinement of the seed is
tracked in overall LP/repair statistics, not attributed to a discrete move.

From the repository root, after a Release build:

```sh
python3 tools/benchmark.py build/cbi --output tmp/coder/replay-short \
  --inputs example/input.cbi --issues --random 30 --budgets 1 --jobs 3
python3 tools/benchmark.py build/cbi --output tmp/coder/replay-long \
  --inputs example/input.cbi benchmarks/sa_lp/inputs/input024.cbi \
  'issues/closed/019 子樹與sink交換可降低buffer型別成本/repro/input001.cbi' \
  --budgets 55 --jobs 3
python3 tools/sweep.py build/cbi --output tmp/coder/replay-training \
  --inputs benchmarks/sa_lp/inputs/input0{00,03,07,08,09,10,11,12}.cbi --jobs 3
python3 tools/sweep.py build/cbi --output tmp/coder/replay-holdout \
  --holdout --inputs benchmarks/sa_lp/inputs/input0{15..29}.cbi --jobs 3
python3 tools/summarize_benchmark.py \
  --short tmp/coder/replay-short --long tmp/coder/replay-long \
  --ablation tmp/coder/replay-training --holdout tmp/coder/replay-holdout \
  --output tmp/coder/replay-summary
```

Brace expansion in the sweep commands requires Bash. Timed runs can produce
different results under different load; use fixed iterations/seeds when checking
algorithmic reproducibility. The recorded experiment binaries preceded final
API-validation, immutable-snapshot ownership and comparison-copy cleanups; those cleanups preserve the search
rules but can affect the number of iterations completed in a timed run.
