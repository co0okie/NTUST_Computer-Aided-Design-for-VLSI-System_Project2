#!/usr/bin/env python3
"""Retry/LP tuning in an isolated copy; never imports or executes the grader."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import statistics
import subprocess
import time

from benchmark import read_problem, verify

ROOT = Path(__file__).resolve().parents[1]
SEEDS = [1, 7, 17, 31, 101]
VARIANTS = {
    "baseline": ({}, []),
    "retry_one": ({"CBI_TUNING_RETRIES": "1"}, []),
    "retry_two": ({"CBI_TUNING_RETRIES": "2"}, []),
    "retry_unique": ({"CBI_TUNING_UNIQUE": "1"}, []),
    "wire_off": ({}, ["--wire-tiebreak", "0"]),
    "dual_objective": ({"CBI_TUNING_DUAL": "1"}, []),
    "lp_005": ({}, ["--lp-time", "0.05"]),
    "lp_010": ({}, ["--lp-time", "0.1"]),
    "reference_020": ({}, ["--current-fraction", "0.2"]),
    "reference_060": ({}, ["--current-fraction", "0.6"]),
}


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected one instrumentation match: {old[:80]!r}")
    return text.replace(old, new, 1)


def prepare(work, revision):
    work.mkdir(parents=True, exist_ok=False)
    project = work / "project"
    for name in filter(None, subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")):
        if (ROOT / name).is_file():
            target = project / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
    shutil.copy2(__file__, project / "tools/tuning.py")
    commit = subprocess.check_output(["git", "rev-parse", revision + "^{commit}"], cwd=ROOT, text=True).strip()
    # Pin the measured production source; retain the current experiment tools
    # and corpus even after a winning policy is promoted into production.
    shutil.rmtree(project / "src")
    source_files = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", "-z", commit,
                                            "--", "src", "CMakeLists.txt"], cwd=ROOT).decode().split("\0")
    for name in filter(None, source_files):
        target = project / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(subprocess.check_output(["git", "show", commit + ":" + name], cwd=ROOT))
    hashes = {}

    def patch(name, change):
        path = project / name
        old = path.read_text()
        new = change(old)
        path.write_text(new)
        hashes[name] = {"original": hashlib.sha256(old.encode()).hexdigest(),
                        "instrumented": hashlib.sha256(new.encode()).hexdigest()}

    def header(s):
        s = replace(s, "struct SearchStatistics {", """struct RetryStatistics {
    std::size_t attempts = 0, successful = 0, improvements = 0;
    std::size_t lp_failed = 0, repair_failed = 0, skipped = 0;
    double seconds = 0;
};
struct SearchStatistics {
    std::array<RetryStatistics, 3> retries{};
    std::size_t last_retry = 0;""")
        return s
    patch("src/search/annealing.hpp", header)

    def annealing(s):
        s = '#include <cstdlib>\n' + s
        s = replace(s, "Expected<SearchState> evaluate_candidate(Problem const &p,", """struct RetryTimer {
    RetryStatistics &statistics;
    TimePoint start = std::chrono::steady_clock::now();
    ~RetryTimer() {
        statistics.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
};
bool tuning_flag(char const *name) {
    auto const value = std::getenv(name);
    return value && std::string(value) == "1";
}
Expected<SearchState> evaluate_candidate_single(Problem const &p,""")
        s = replace(s, "    for (unsigned attempt = 0; attempt < 3; ++attempt) {", """    auto const raw_retries = std::getenv("CBI_TUNING_RETRIES");
    auto const attempts = raw_retries ? static_cast<unsigned>(std::stoul(raw_retries)) : 3U;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {""")
        s = replace(s, "        if (attempt > 0) {\n            auto const capacity", """        auto &retry = statistics.retries[attempt];
        RetryTimer timer{retry};
        if (attempt > 0) {
            auto const previous = state.capacity;
            auto const capacity""")
        s = replace(s, "            if (attempt == 2) {", """            if (attempt == 1 && tuning_flag("CBI_TUNING_UNIQUE") &&
                std::equal(previous.begin(), previous.end(), state.capacity.begin(),
                           [](auto a, auto b) { return (!a && !b) || (a && b && a->value == b->value); })) {
                ++retry.skipped;
                continue;
            }
            if (attempt == 2) {""")
        s = replace(s, "        auto const start = std::chrono::steady_clock::now();\n        ++statistics.lp_calls;", "        ++retry.attempts;\n        auto const start = std::chrono::steady_clock::now();\n        ++statistics.lp_calls;")
        s = replace(s, "        if (is_error(placement)) {\n            last", "        if (is_error(placement)) {\n            ++retry.lp_failed;\n            last")
        s = replace(s, "            ++statistics.repair_feasible;\n            return repaired;", "            ++statistics.repair_feasible;\n            ++retry.successful;\n            statistics.last_retry = attempt;\n            return repaired;")
        s = replace(s, "        last = std::get<Error>(repaired);", "        ++retry.repair_failed;\n        last = std::get<Error>(repaired);")
        s = replace(s, "} // namespace\nExpected<WriteSuccess> validate_annealing_options", """Expected<SearchState> evaluate_candidate(Problem const &p, SearchState state,
                                         AnnealingOptions const &options, TimePoint deadline,
                                         SearchStatistics &statistics, bool preserve_type) {
    if (!tuning_flag("CBI_TUNING_DUAL"))
        return evaluate_candidate_single(p, std::move(state), options, deadline, statistics, preserve_type);
    auto first = evaluate_candidate_single(p, state, options, deadline, statistics, preserve_type);
    auto const first_retry = statistics.last_retry;
    auto alternate = options;
    alternate.placement.minimize_wire = !options.placement.minimize_wire;
    auto second = evaluate_candidate_single(p, std::move(state), alternate, deadline, statistics, preserve_type);
    if (is_error(first))
        return second;
    if (!is_error(second) && validate_tree(p, std::get<SearchState>(second).tree).score <
                                validate_tree(p, std::get<SearchState>(first).tree).score)
        return second;
    statistics.last_retry = first_retry;
    return first;
}
} // namespace
Expected<WriteSuccess> validate_annealing_options""")
        s = replace(s, "                    if (metrics.score < result.metrics.score)\n                        ++stats.improvements;", """                    if (metrics.score < result.metrics.score) {
                        ++stats.improvements;
                        ++result.statistics.retries[result.statistics.last_retry].improvements;
                    }""")
        return s
    patch("src/search/annealing.cpp", annealing)

    def strategy(s):
        return replace(s, '    out << "],\\n\\\"checkpoints\\\":[";', '''    out << "],\\n\\\"retries\\\":[";
    for (std::size_t i = 0; i < r.statistics.retries.size(); ++i) {
        if (i) out << ',';
        auto const &s = r.statistics.retries[i];
        out << "{\\\"attempts\\\":" << s.attempts << ",\\\"successful\\\":" << s.successful
            << ",\\\"improvements\\\":" << s.improvements << ",\\\"lp_failed\\\":" << s.lp_failed
            << ",\\\"repair_failed\\\":" << s.repair_failed << ",\\\"skipped\\\":" << s.skipped
            << ",\\\"seconds\\\":" << s.seconds << '}';
    }
    out << "],\\n\\\"checkpoints\\\":[";''')
    patch("src/solver/strategy.cpp", strategy)
    training = [f"benchmarks/sa_lp/inputs/input{i:03}.cbi" for i in [0, 3, 6, 9, 12, 14]]
    training += ["example/input.cbi", str(next((ROOT / "issues/closed").glob("019 */repro/input001.cbi")).relative_to(ROOT))]
    holdout = [f"benchmarks/sa_lp/inputs/input{i:03}.cbi" for i in [15, 19, 23, 27]]
    protocol = {"source_commit": commit,
                "source_patches": hashes, "seeds": SEEDS, "variants": VARIANTS,
                "training": training, "holdout": holdout,
                "screen": {"iterations": 100, "seconds": 25},
                "confirm": {"iterations": 0, "seconds": 25},
                "selection": "Choose the non-baseline variant with the smallest mean of per-case five-seed median relative Score deltas; prefer fewer median regressions, then faster median time. A non-improving winner is exploratory, not a deployment recommendation.",
                "constraints": "Ubuntu24 GCC13 default build; network off; 2 CPUs/8GiB; single solver at a time; independent legal verification; external 30s timeout"}
    (work / "protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")
    (project / "tuning-protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")
    print(project, flush=True)


def run_case(root, output, phase, case, variant, seed, iterations):
    identity = [phase, case, variant, seed]
    key = hashlib.sha256(json.dumps(identity).encode()).hexdigest()[:16]
    record = output / (key + ".json")
    if record.exists():
        return json.loads(record.read_text())
    variables, extra = VARIANTS[variant]
    result, stats = output / (key + ".cbi"), output / (key + ".stats.json")
    environment = os.environ.copy()
    for name in ["CBI_TUNING_RETRIES", "CBI_TUNING_UNIQUE", "CBI_TUNING_DUAL"]:
        environment.pop(name, None)
    environment.update(variables)
    command = [str(root / "build/cbi"), str(root / case), str(result), "--strategy", "hybrid",
               "--time", "25", "--seed", str(seed), "--stats", str(stats)] + extra
    if iterations:
        command += ["--iterations", str(iterations)]
    start = time.monotonic()
    row = {"phase": phase, "input": case, "variant": variant, "seed": seed,
           "iterations": iterations, "budget": 25, "options": extra, "environment": variables, "legal": False}
    try:
        process = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
        row.update(returncode=process.returncode, stdout=process.stdout, stderr=process.stderr)
        if process.returncode == 0:
            row.update(verify(read_problem(root / case), result.read_text()))
            row["statistics"] = json.loads(stats.read_text())
            assert row["statistics"]["score"] == row["score"]
            assert f'Score: {row["score"]}' in process.stdout
            row["legal"] = True
    except (subprocess.TimeoutExpired, AssertionError, ValueError, KeyError) as error:
        row["error"] = repr(error)
    row["seconds"] = time.monotonic() - start
    pending = record.with_suffix(".pending")
    pending.write_text(json.dumps(row, indent=2) + "\n")
    pending.replace(record)
    return row


def rows(output):
    return [json.loads(p.read_text()) for p in output.glob("*.json") if len(p.stem) == 16]


def verify_baseline(root, output, baseline):
    os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:2]))
    output.mkdir(parents=True, exist_ok=False)
    protocol = json.loads((root / "tuning-protocol.json").read_text())
    records = []
    for i, case in enumerate(protocol["training"] + protocol["holdout"]):
        for strategy in ["sa_lp", "hybrid"]:
            contents, scores = [], []
            for label, binary, extra in [("production", baseline, {}),
                                         ("instrumented", root / "build/cbi", {}),
                                         ("retry_unique", root / "build/cbi", {"CBI_TUNING_UNIQUE": "1"})]:
                target = output / f"{i}-{strategy}-{label}.cbi"
                environment = os.environ.copy()
                for name in ["CBI_TUNING_RETRIES", "CBI_TUNING_UNIQUE", "CBI_TUNING_DUAL"]:
                    environment.pop(name, None)
                environment.update(extra)
                process = subprocess.run([str(binary), str(root / case), str(target), "--strategy", strategy,
                                          "--iterations", "100", "--time", "25", "--seed", "7"],
                                         env=environment, capture_output=True, text=True, timeout=30)
                if process.returncode:
                    raise ValueError((case, strategy, label, process.stderr))
                metrics = verify(read_problem(root / case), target.read_text())
                contents.append((target.read_text(), process.stdout))
                scores.append(metrics["score"])
            if contents[0] != contents[1]:
                raise ValueError((case, strategy, "instrumentation changed output"))
            records.append({"input": case, "strategy": strategy, "baseline_identical": True,
                            "unique_identical": contents[0] == contents[2], "scores": scores})
    (output / "verification.json").write_text(json.dumps(records, indent=2) + "\n")
    print(json.dumps({"baseline_equivalence": len(records),
                      "unique_identical": sum(r["unique_identical"] for r in records), "all_legal": True}), flush=True)


def summarize(output):
    data = rows(output)
    fields = ["phase", "input", "variant", "seed", "iterations", "budget", "legal", "score", "skew", "cost", "buffers", "seconds"]
    with (output / "runs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fields, extrasaction="ignore", lineterminator="\n")
        writer.writeheader()
        writer.writerows(sorted(data, key=lambda r: (r["phase"], r["input"], r["variant"], r["seed"])))
    retry_fields = ["phase", "input", "variant", "seed", "attempt", "attempts", "successful", "improvements", "lp_failed", "repair_failed", "skipped", "seconds"]
    with (output / "retry-runs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, retry_fields, lineterminator="\n")
        writer.writeheader()
        for row in sorted(data, key=lambda r: (r["phase"], r["input"], r["variant"], r["seed"])):
            for i, retry in enumerate(row.get("statistics", {}).get("retries", [])):
                writer.writerow({**{k: row[k] for k in ["phase", "input", "variant", "seed"]}, "attempt": i+1, **retry})
    summary = []
    for phase in sorted({r["phase"] for r in data}):
        base = {(r["input"], r["seed"]): r for r in data if r["phase"] == phase and r["variant"] == "baseline"}
        for variant in sorted({r["variant"] for r in data if r["phase"] == phase}):
            selected = [r for r in data if r["phase"] == phase and r["variant"] == variant]
            deltas, time_ratios = [], []
            case_deltas = []
            for case in sorted({r["input"] for r in selected}):
                group = [r for r in selected if r["input"] == case]
                paired = [(r, base.get((case, r["seed"]))) for r in group]
                if len(group) == len(SEEDS) and all(r["legal"] and b and b["legal"] for r, b in paired):
                    a = statistics.median(r["score"] for r, b in paired)
                    b = statistics.median(b["score"] for r, b in paired)
                    case_deltas.append((a-b) / max(1, b))
                for r, b in paired:
                    if r["legal"] and b and b["legal"]:
                        deltas.append(r["score"]-b["score"])
                        time_ratios.append(r["seconds"] / b["seconds"])
            summary.append({"phase": phase, "variant": variant, "runs": len(selected),
                            "legal": sum(r["legal"] for r in selected),
                            "seed_better": sum(d < 0 for d in deltas), "seed_equal": sum(d == 0 for d in deltas), "seed_worse": sum(d > 0 for d in deltas),
                            "case_better": sum(d < 0 for d in case_deltas), "case_equal": sum(d == 0 for d in case_deltas), "case_worse": sum(d > 0 for d in case_deltas),
                            "complete_cases": len(case_deltas),
                            "mean_case_relative_delta": statistics.mean(case_deltas) if case_deltas else None,
                            "median_time_ratio": statistics.median(time_ratios) if time_ratios else None})
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    retry_summary = {}
    for phase in sorted({r["phase"] for r in data}):
        retry_summary[phase] = []
        for i in range(3):
            selected = [r["statistics"]["retries"][i] for r in data if r["phase"] == phase and r["variant"] == "baseline" and r["legal"]]
            retry_summary[phase].append({key: sum(r[key] for r in selected) for key in ["attempts", "successful", "improvements", "lp_failed", "repair_failed", "skipped", "seconds"]})
    (output / "retries.json").write_text(json.dumps(retry_summary, indent=2) + "\n")
    return summary


def run(root, output, phase):
    os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:2]))
    output.mkdir(parents=True, exist_ok=True)
    protocol = json.loads((root / "tuning-protocol.json").read_text())
    manifest = {"binary_sha256": hashlib.sha256((root / "build/cbi").read_bytes()).hexdigest(),
                "input_sha256": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in protocol["training"] + protocol["holdout"]},
                "cpu_affinity": sorted(os.sched_getaffinity(0)), "protocol": protocol}
    if (output / "metadata.json").exists():
        if json.loads((output / "metadata.json").read_text()) != manifest:
            raise ValueError("Study source, binary, corpus or protocol changed")
    else:
        (output / "metadata.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if phase == "screen":
        plans = [(case, variant, seed) for case in protocol["training"] for variant in VARIANTS for seed in SEEDS]
        iterations = 100
    else:
        summary = summarize(output)
        candidates = [r for r in summary if r["phase"] == "screen" and r["variant"] != "baseline" and r["complete_cases"] == len(protocol["training"]) and r["legal"] == r["runs"]]
        if not candidates:
            raise ValueError("Finish the fixed-iteration screen before confirmation")
        winner = min(candidates, key=lambda r: (r["mean_case_relative_delta"], r["case_worse"], r["median_time_ratio"]))
        selection = {"challenger": winner["variant"], "screen_result": winner, "training_only_selection": True}
        (output / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
        plans = [(case, variant, seed) for case in protocol["holdout"] for variant in ["baseline", winner["variant"]] for seed in SEEDS]
        iterations = 0
    random.Random(20261013 if phase == "screen" else 20261014).shuffle(plans)
    started = time.monotonic()
    for i, (case, variant, seed) in enumerate(plans):
        row = run_case(root, output, phase, case, variant, seed, iterations)
        print(json.dumps({"phase": phase, "completed": i+1, "total": len(plans), "variant": variant,
                          "input": case, "seed": seed, "legal": row["legal"], "score": row.get("score"),
                          "seconds": row["seconds"], "elapsed": time.monotonic()-started}), flush=True)
        if (i+1) % 20 == 0:
            summarize(output)
    summarize(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["prepare", "verify", "screen", "confirm", "export"])
    parser.add_argument("--work", type=Path)
    parser.add_argument("--revision", default="4552689", help="prepare source revision (default: this study's baseline)")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    if args.phase == "prepare":
        if args.work is None:
            parser.error("prepare requires --work")
        prepare(args.work.resolve(), args.revision)
    elif args.phase == "verify":
        if args.output is None or args.baseline is None:
            parser.error("verify requires --output and --baseline")
        verify_baseline(args.root.resolve(), args.output.resolve(), args.baseline.resolve())
    elif args.phase == "export":
        if args.output is None:
            parser.error("export requires --output")
        summarize(args.output.resolve())
    else:
        if args.output is None:
            parser.error("screen/confirm require --output")
        run(args.root.resolve(), args.output.resolve(), args.phase)


if __name__ == "__main__":
    main()
