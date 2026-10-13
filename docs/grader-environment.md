# Ubuntu 24.04 environment validation

Validated on 2026-10-13 against source commit
`ead7a38abae096ed74039acb5f0503294390199e`, followed by the build-only change
making `BUILD_TESTING` default to `OFF`. Solver source and bundled archives were
unchanged. The grading archive was read to identify its environment and commands;
none of its programs were executed or imported.

## Environment

The grading reference describes Ubuntu 24.04, two virtual CPUs, 8 GB RAM,
no network, an ordinary `grader` user, a combined 60-second configure/build
limit, and a separate 60-second limit per input. Its build commands do not
specify Release or parallel compilation.

The compatibility run used Podman with Ubuntu 24.04.5 x86-64, GCC/G++ 13.3,
CMake 3.28.3, Python 3.12.3 and glibc 2.39. The container ran as UID 1000,
with `--network none`, `--cpus 2`, `--memory 8g`, and `--memory-swap 8g`.
Observed cgroup limits were `cpu.max = 200000 100000` and
`memory.max = 8589934592`; only loopback networking was present. The test
process and its children were pinned to two host CPUs using Python
`os.sched_setaffinity`, since the rootless host did not delegate the cpuset
controller.

Only tracked project files were copied into a fresh submission-like directory.
No existing build, input/output cache, host compiler or host HiGHS installation
was reused. System packages were installed while preparing the image; project
configure, compilation and execution all ran without networking.

## Build and project tests

Commands matched the grader's two build steps:

```sh
cmake -S /home/grader/M00000000 -B /home/grader/M00000000/build
cmake --build /home/grader/M00000000/build
```

Both steps shared one 60-second deadline. `CMAKE_BUILD_TYPE` was empty and
compilation was serial. The initial build also compiled test executables and
took 58.96 seconds. With tests made opt-in, a separate clean build took
49.96 seconds: 1.22 seconds configuring and 48.74 seconds compiling/linking.
The observed peak child RSS was approximately 246 MiB.

The default build now skips test executables. Existing build directories keep
their cached `BUILD_TESTING` setting; tests remain available using
`-DBUILD_TESTING=ON`. All four project CTest suites passed in the same container:

| Suite | Seconds |
| --- | ---: |
| Search | 0.87 |
| Annealing and LP | 0.40 |
| Solver regression | 166.96 |
| CLI | 1.29 |

These tests include actual HiGHS placement optimization, comparison with small
integer enumerations, deadline handling, all strategies and output validation.
`ldd build/cbi` resolved only Ubuntu's normal C++/math/GCC/C runtime libraries;
HiGHS was linked statically. Both bundled archive checksums matched.

## Execution results

Five distinct inputs passed the exact two-positional-argument invocation,
`build/cbi INPUT OUTPUT`, with stdin closed and an external 60-second kill
limit. No strategy/time flags were supplied:

| Input | Seconds | Score | Independent validation |
| --- | ---: | ---: | --- |
| example/input.cbi | 55.02 | 37 | Passed |
| benchmarks/sa_lp/inputs/input024.cbi | 55.01 | 56 | Passed |
| issues/closed/019 子樹與sink交換可降低buffer型別成本/repro/input001.cbi | 55.01 | 69 | Passed |
| issues/closed/018 大樹跳過多輸出改善保留三倍buffer成本/input001.cbi | 0.81 | 341 | Passed |
| issues/closed/021 固定三根merge漏掉合法四五輸出forest/repro/input003.cbi | 55.01 | 203 | Passed |

The approximately 55-second runs use the default hybrid search budget; they
are not measurements of how long initial construction requires. For example,
the supplied example took 0.16 seconds with `current` (Score 41), while default
hybrid used 55.02 seconds (Score 37). The 018 input reached the proven lower
bound and stopped in 0.81 seconds even with the default budget.

A separate short-budget suite covered 79 inputs: the example, 48 issue inputs,
and 30 recorded random inputs. Every strategy used `--time 1 --seed 7`, with
one process at a time:

| Strategy | Legal outputs / runs |
| --- | ---: |
| current | 79 / 79 |
| sa_lp | 79 / 79 |
| hybrid | 77 / 79 |

All successful outputs passed the project's independent integer verifier:
unique/bounded coordinates, connected acyclic tree, fanout, summed driver wire,
levels, arrival times, buffer cost and Score. Across these runs, statistics
recorded 25,471 LP calls and 16,281 feasible LP results.

The two one-second hybrid failures were issue 018 input001 and issue 021
input003, both reporting `construction deadline reached`. Hybrid reserves
40% of its budget for initial/reference construction, so these invocations had
only about 0.4 seconds to obtain a legal initial tree. Both inputs passed again
with the default invocation and with `--time 5`; their five-second-mode process
times were 0.63 and 5.01 seconds. These are actual short-budget limitations,
not library/linker failures. Solver algorithms were not changed to hide them.

## Reproducing the container

The preparation recipe is [tools/grader-env/Containerfile](../tools/grader-env/Containerfile):

```sh
podman build --pull=never -t localhost/cbi-grader-ubuntu24 \
  -f tools/grader-env/Containerfile tools/grader-env
mkdir -p tmp/coder/grader-environment/project
git archive HEAD | tar -x -C tmp/coder/grader-environment/project
podman run --rm --network none --cpus 2 --memory 8g --memory-swap 8g \
  --userns keep-id --user 1000:1000 -w /home/grader/M00000000 \
  -v "$PWD/tmp/coder/grader-environment/project:/home/grader/M00000000:Z" \
  localhost/cbi-grader-ubuntu24 bash
```

The base Ubuntu image must already be available for `--pull=never`; package
installation needs network access during image preparation. The shell command
above opens the prepared image with the project copy mounted; run the build
commands and `timeout -s KILL 60 build/cbi INPUT OUTPUT` there. For the strict
combined build deadline, compute the remaining time before each build step.
Two-CPU affinity must be applied separately if reproducing that detail.

Local raw logs, JSON results, the independent checking script and isolated
source/build copies are under `tmp/coder/grader-env-20261013/` and are ignored
by Git.

## Limits of this check

This verifies Ubuntu userspace compatibility, offline building and the tested
executions. Podman shares the host kernel; it does not reproduce the actual
QEMU VM, its precise package revisions, CPU speed, disk performance or hidden
inputs. The measured build/runtime margins are specific to this machine and
workload, rather than guarantees for every grading run.
