#!/usr/bin/env python3
"""Controlled leave-one-out experiments. Grader code is never imported/executed.

prepare creates an instrumented source COPY; production sources are unchanged.
Run that copy in the documented Ubuntu container, one solver process at a time.
"""
import argparse
import csv
import hashlib
import json
import os
import pathlib
import random
import re
import shutil
import subprocess
import time
from collections import Counter

from benchmark import read_problem, verify, generate

ROOT = pathlib.Path(__file__).resolve().parents[1]
SEEDS = [1, 7, 17]

LEGACY = {"baseline": []}
for gate in ("layered", "compressed", "multiway", "reference_tree", "dense", "small_forest",
             "local_search", "adaptive", "final_delay", "joint_hub", "joint_bypass", "large_contract"):
    LEGACY["no_" + gate] = [gate]

SEARCH = {
    "baseline": ([], []),
    "no_initial_pass": (["initial_pass"], []),
    "no_reference_pass": (["reference_pass"], []),
    "no_sa": (["sa"], []),
    "no_lp": (["lp"], []),
    "no_seed_lp": (["seed_lp"], []),
    "no_capacity_retry": (["capacity_retry"], []),
    "greedy": (["uphill"], []),
    "no_structural_repair": ([], ["--structural-repair", "0"]),
    "no_integer_repair": ([], ["--repair-trials", "0"]),
    "no_locality": ([], ["--locality", "0"]),
    "no_wire_lp": ([], ["--wire-tiebreak", "0"]),
}
MOVE_NAMES = ["move_sink", "swap_sinks", "insert_buffer", "delete_buffer",
              "change_direction", "change_type", "move_subtree"]
for i, name in enumerate(MOVE_NAMES):
    weights = [25, 15, 15, 10, 20, 10, 5]
    weights[i] = 0
    SEARCH["no_" + name] = ([], ["--move-weights", ",".join(map(str, weights))])

COMBINATIONS = {"lean_initialization": (["initial_pass", "seed_lp"], []),
                "no_final_delay": (["final_delay"], [])}

HOOK_HEADER = r'''#pragma once
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
namespace cbi {
inline bool study_enabled(char const* gate) {
    static auto const disabled = [] {
        std::set<std::string> result;
        if (auto raw = std::getenv("CBI_STUDY_DISABLE")) {
            std::istringstream stream(raw);
            std::string word;
            while (std::getline(stream, word, ',')) result.insert(word);
        }
        return result;
    }();
    return !disabled.count(gate);
}
}
'''


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"Expected one exact hook match: {before[:100]!r}; got {text.count(before)}")
    return text.replace(before, after, 1)


def entry(text, name, statement):
    pattern = r"(?m)^(?:Tree|std::optional<Tree>)\s+" + re.escape(name) + r"\([^{};]*\)\s*\{"
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {name}; got {len(matches)}")
    at = matches[0].end()
    return text[:at] + "\n    " + statement + text[at:]


def prepare(destination):
    if destination.exists():
        raise ValueError("Use a new empty study directory; existing evidence is preserved")
    destination.mkdir(parents=True)
    project = destination / "project"
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
    for name in filter(None, files):
        source = ROOT / name
        if source.is_file():
            target = project / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    # This script may be new/untracked while the experiment is being prepared.
    shutil.copy2(__file__, project / "tools/ablation.py")
    (project / "src/study_hooks.hpp").write_text(HOOK_HEADER)
    patches = {}
    def patch(path, change):
        file = project / path
        before = file.read_text()
        after = '#include "study_hooks.hpp"\n' + change(before)
        file.write_text(after)
        patches[path] = {"original_sha256": hashlib.sha256(before.encode()).hexdigest(),
                         "instrumented_sha256": hashlib.sha256(after.encode()).hexdigest()}

    def legacy(s):
        for name, gate, result in [("layered_candidate", "layered", "{}"),
                                  ("compressed_chain_candidate", "compressed", "{}"),
                                  ("partial_hub_tree", "joint_hub", "best"),
                                  ("joint_bypass_tree", "joint_bypass", "best")]:
            s = entry(s, name, f'if (!study_enabled("{gate}")) return {result};')
        s = entry(s, "solve_reference", 'if (!study_enabled("reference_tree")) throw std::runtime_error("study reference tree disabled");')
        s = replace_once(s, 'if (p.sinks.size() < component_budget && p.buffer_library.size() <= 64)',
                         'if (study_enabled("multiway") && p.sinks.size() < component_budget && p.buffer_library.size() <= 64)')
        return s
    patch("src/solver/legacy.cpp", legacy)
    def forest(s):
        for name, gate in [("small_forest", "small_forest"), ("dense_forest", "dense")]:
            s = entry(s, name, f'if (!study_enabled("{gate}")) return {{}};')
        return s
    patch("src/construction/forest.cpp", forest)
    def local(s):
        for name in ["improve_tree", "relocate_tree", "swap_roots", "reparent_leaves", "delay_tree", "refine_tree"]:
            s = entry(s, name, 'if (!study_enabled("local_search")) return best;')
        s = entry(s, "adaptive_refine", 'if (!study_enabled("local_search") || !study_enabled("adaptive")) return best;')
        s = entry(s, "final_delay", 'if (!study_enabled("local_search") || !study_enabled("final_delay")) return best;')
        return entry(s, "contract_large_tree", 'if (!study_enabled("large_contract")) return original;')
    patch("src/search/local.cpp", local)
    def strategy(s):
        s = replace_once(s, 'auto initial = options.strategy == Strategy::current',
                         'auto initial = options.strategy == Strategy::current || !study_enabled("initial_pass")')
        s = replace_once(s, 'if (options.strategy != Strategy::sa_lp) {',
                         'if (options.strategy != Strategy::sa_lp && study_enabled("reference_pass")) {')
        return replace_once(s, 'else if (options.strategy != Strategy::current && std::chrono::steady_clock::now() < deadline)',
                            'else if (options.strategy != Strategy::current && study_enabled("sa") && std::chrono::steady_clock::now() < deadline)')
    patch("src/solver/strategy.cpp", strategy)
    def annealing(s):
        s = replace_once(s, 'attempt < 3;', 'attempt < (study_enabled("capacity_retry") ? 3U : 1U);')
        s = replace_once(s, '++statistics.lp_calls;\n        auto placement = optimize_placement(p, state, options.placement, deadline);', '''if (study_enabled("lp")) ++statistics.lp_calls;
        auto placement = [&]() -> Expected<Placement> {
            if (study_enabled("lp")) return optimize_placement(p, state, options.placement, deadline);
            Placement existing{{}, 0};
            for (auto const& node : state.tree)
                existing.positions.push_back({static_cast<double>(node.position.x), static_cast<double>(node.position.y)});
            return existing;
        }();''')
        s = replace_once(s, '++statistics.lp_feasible;', 'if (study_enabled("lp")) ++statistics.lp_feasible;')
        s = replace_once(s, 'auto refined = evaluate_candidate(p, current, options, deadline, result.statistics, true);',
                         'auto refined = study_enabled("seed_lp") ? evaluate_candidate(p, current, options, deadline, result.statistics, true) : Expected<SearchState>{Error{"study seed LP disabled", ErrorKind::solver}};')
        return replace_once(s, 'delta <= 0 || unit(rng) < std::exp(-static_cast<double>(delta) / t)',
                            'delta <= 0 || (study_enabled("uphill") && unit(rng) < std::exp(-static_cast<double>(delta) / t))')
    patch("src/search/annealing.cpp", annealing)
    metadata = {"source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "patches": patches, "seeds": SEEDS, "legacy_gates": LEGACY, "search_variants": SEARCH,
                "purpose": "Temporary instrumentation only; no production source modifications"}
    (destination / "instrumentation.json").write_text(json.dumps(metadata, indent=2))
    print(project, flush=True)


def paths(root):
    return [root / "example/input.cbi"] + sorted((root / "issues").rglob("input*.cbi")) + sorted((root / "benchmarks/sa_lp/inputs").glob("input*.cbi"))


def execute(root, output, phase, case, variant, seed, disabled, options, *, iterations=0, seconds=25):
    identity = [phase, str(case.relative_to(root)), variant, seed, iterations, seconds]
    key = hashlib.sha256(json.dumps(identity).encode()).hexdigest()[:16]
    record = output / (key + ".json")
    if record.exists():
        cached = json.loads(record.read_text())
        if cached["disabled"] != disabled or cached["options"] != options:
            raise ValueError("Cached run has different controls; use a new output directory")
        return cached
    result_file, stats_file = output / (key + ".cbi"), output / (key + ".stats.json")
    command = [str(root / "build/cbi"), str(case), str(result_file), "--time", str(seconds),
               "--seed", str(seed), "--stats", str(stats_file)] + options
    if iterations:
        command += ["--iterations", str(iterations)]
    environment = os.environ.copy()
    environment["CBI_STUDY_DISABLE"] = ",".join(disabled)
    start = time.monotonic()
    try:
        run = subprocess.run(command, env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
        rc, stdout, stderr = run.returncode, run.stdout, run.stderr
    except subprocess.TimeoutExpired:
        rc, stdout, stderr = 137, "", "external 30-second timeout"
    row = {"phase": phase, "input": str(case.relative_to(root)), "variant": variant, "seed": seed,
           "iterations": iterations, "budget": seconds, "disabled": disabled, "options": options,
           "seconds": time.monotonic() - start, "returncode": rc, "legal": False}
    if rc == 0:
        try:
            row.update(verify(read_problem(case), result_file.read_text()))
            assert f'Score: {row["score"]}' in stdout
            row["statistics"] = json.loads(stats_file.read_text())
            assert row["statistics"]["score"] == row["score"]
            row["legal"] = True
        except (AssertionError, ValueError, KeyError) as error:
            row["error"] = "independent validation: " + repr(error)
    else:
        row["error"] = stderr
    pending = record.with_suffix(".pending")
    pending.write_text(json.dumps(row, indent=2))
    pending.replace(record)
    (output / (key + ".log")).write_text(stdout + "\n" + stderr)
    return row


def read_rows(output):
    return [json.loads(p.read_text()) for p in output.glob("*.json") if re.fullmatch(r"[0-9a-f]{16}", p.stem)]


def training_cases(root, rows):
    # Predetermined split: generated IDs 015..029 are held out, not used here.
    baseline = [r for r in rows if r["phase"] == "legacy" and r["variant"] == "baseline"]
    eligible = []
    for r in baseline:
        if not r["legal"] or r["statistics"]["termination"] in [2, 4]:
            continue
        p = pathlib.Path(r["input"])
        if "benchmarks" in p.parts and int(p.stem[-3:]) >= 15:
            continue
        problem = read_problem(root / p)
        if len(problem[-1]) > 129 or max(problem[0]['.dimx'], problem[0]['.dimy']) > 2**40:
            continue
        eligible.append(root / p)
    return sorted(eligible)


def run_phase(root, output, phase):
    output.mkdir(parents=True, exist_ok=True)
    os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:2]))
    manifest = output / "metadata.json"
    if manifest.exists() and json.loads(manifest.read_text())["binary_sha256"] != hashlib.sha256((root / "build/cbi").read_bytes()).hexdigest():
        raise ValueError("Study executable changed; use a new output directory")
    if not manifest.exists():
        manifest.write_text(json.dumps({
            "binary_sha256": hashlib.sha256((root / "build/cbi").read_bytes()).hexdigest(),
            "input_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths(root)},
            "legacy_variants": LEGACY, "search_variants": SEARCH, "seeds": SEEDS,
            "single_process": True, "compiler_build_type": "default, no Release flag",
            "cpu_affinity": sorted(os.sched_getaffinity(0)),
            "protocol": "legacy 25-second safety cap; screening 100 iterations and 25-second safety cap; confirmation 5 seconds",
        }, indent=2))
    if phase == "verify":
        # All hooks enabled must preserve the production binary exactly.
        for i, p in enumerate(paths(root)):
            a = execute(root, output, phase, p, "baseline", 7, [], ["--strategy", "current"])
            assert a["legal"], a
            expected = output / f'normal-{i}.cbi'
            normal = subprocess.run(["/home/grader/normal-cbi", str(p), str(expected), "--strategy", "current", "--time", "25"],
                                    stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
            assert normal.returncode == 0, (p, normal.stderr)
            assert expected.read_text() == (output / (hashlib.sha256(json.dumps([phase, str(p.relative_to(root)), "baseline", 7, 0, 25]).encode()).hexdigest()[:16] + '.cbi')).read_text(), p
        for p in [root / "example/input.cbi", root / "benchmarks/sa_lp/inputs/input024.cbi",
                  next((root / "issues/closed").glob("019 */repro/input001.cbi"))]:
            for strategy in ["sa_lp", "hybrid"]:
                a = execute(root, output, "verify_sa", p, strategy, 7, [], ["--strategy", strategy], iterations=100)
                assert a["legal"], a
                key = hashlib.sha256(json.dumps(["verify_sa", str(p.relative_to(root)), strategy, 7, 100, 25]).encode()).hexdigest()[:16]
                expected = output / (key + "-normal.cbi")
                normal = subprocess.run(["/home/grader/normal-cbi", str(p), str(expected), "--strategy", strategy,
                                         "--iterations", "100", "--seed", "7", "--time", "25"],
                                        stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
                assert normal.returncode == 0 and expected.read_text() == (output / (key + ".cbi")).read_text(), (p, strategy)
        print("All-enabled study hooks preserve 79 current + 6 SA/hybrid production outputs", flush=True)
        return
    plans = []
    if phase == "legacy":
        for p in paths(root):
            for variant, gates in LEGACY.items():
                plans.append((p, variant, 7, gates, ["--strategy", "current"], 0, 25))
    elif phase == "legacy_holdout":
        folder = root / "benchmarks/ablation-holdout/inputs"
        fresh = generate(folder, 100, 20261014)
        for i in range(6):
            rng = random.Random(20261014 + i)
            points = [(x, y) for x in range(101) for y in range(101) if (x, y) != (0, 0)]
            sinks = rng.sample(points, 600 if i < 3 else 1000)
            text = '.limit\nfanout 2\nlength 20\n.dimx 100\n.dimy 100\n.e\n.buflib 2\nT0 4 20 1\nT1 8 64 3\n.e\n.objective\nw_skew 100\n.e\n'
            text += f'.pin {len(sinks)+1}\n0 0\n' + ''.join(f'{x} {y}\n' for x,y in sinks) + '.e\n'
            file = folder / f'large{i:03}.cbi';file.write_text(text);fresh.append(file)
        for p in fresh:
            for variant,gates in [("baseline",[]),("no_final_delay",["final_delay"]),
                                  ("no_large_contract",["large_contract"]),
                                  ("no_tail",["final_delay","large_contract"])]:
                plans.append((p,variant,7,gates,["--strategy","current"],0,25))
        manifest = json.loads((output / "metadata.json").read_text())
        manifest["legacy_holdout"] = {"generation_seed":20261014,"random_count":100,"large_count":6,
            "input_sha256":{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in fresh}}
        (output / "metadata.json").write_text(json.dumps(manifest,indent=2))
    elif phase == "init_stress":
        folder = root / "benchmarks/ablation-stress/inputs";folder.mkdir(parents=True,exist_ok=True)
        fresh=[]
        for i,count in enumerate([256,512,1024,2048]):
            rng=random.Random(20261015+i);pins={(0,0)}
            while len(pins)<count+1: pins.add((rng.randrange(1000001),rng.randrange(1000001)))
            sinks=sorted(pins-{(0,0)})
            text='.limit\nfanout 1\nlength 20\n.dimx 1000000\n.dimy 1000000\n.e\n.buflib 3\nT0 2 1000000 1\nT1 3 64 2\nT2 64 1024 1000\n.e\n.objective\nw_skew 100\n.e\n'
            text+=f'.pin {count+1}\n0 0\n'+''.join(f'{x} {y}\n' for x,y in sinks)+'.e\n'
            file=folder/f'input{i:03}.cbi';file.write_text(text);fresh.append(file)
        for p in fresh:
            for seconds in [0.25,0.5,1,2]:
                for variant,gates in [("baseline",[]),("no_initial_pass",["initial_pass"]),("lean_initialization",["initial_pass","seed_lp"])]:
                    plans.append((p,variant,7,gates,["--strategy","hybrid"],0,seconds))
        manifest=json.loads((output/'metadata.json').read_text())
        manifest['initialization_stress']={'seed':20261015,'sizes':[256,512,1024,2048],
            'budgets':[0.25,0.5,1,2],
            'reason':'Adversarial mix of a cheap long binary type and expensive/high-fanout short types. Test loss of the cheap incumbent when the expanded construction pipeline uses its time slice.',
            'input_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in fresh}}
        (output/'metadata.json').write_text(json.dumps(manifest,indent=2))
    elif phase == "screen":
        cases = training_cases(root, read_rows(output))
        (output / "screen-cases.txt").write_text("\n".join(str(p.relative_to(root)) for p in cases) + "\n")
        for p in cases:
            for variant, (gates, extra) in SEARCH.items():
                for seed in SEEDS:
                    plans.append((p, variant, seed, gates, ["--strategy", "hybrid"] + extra, 100, 25))
    elif phase == "confirm":
        # Confirmation is specified separately after screening; baseline always included.
        spec = json.loads((output / "confirmation-plan.json").read_text())
        manifest = json.loads((output / "metadata.json").read_text())
        manifest["confirmation"] = {**spec, "combinations": COMBINATIONS}
        (output / "metadata.json").write_text(json.dumps(manifest,indent=2))
        for relative in spec["cases"]:
            p = root / relative
            for variant in spec["variants"]:
                gates, extra = {**SEARCH, **COMBINATIONS}[variant]
                for seed in SEEDS:
                    plans.append((p, variant, seed, gates, ["--strategy", "hybrid"] + extra, 0, 5))
    random.Random(20261013).shuffle(plans)
    for i, (p, variant, seed, gates, opts, iterations, seconds) in enumerate(plans):
        r = execute(root, output, phase, p, variant, seed, gates, opts, iterations=iterations, seconds=seconds)
        print(f'{phase} {i+1}/{len(plans)} {variant} {p.relative_to(root)} seed={seed} legal={r["legal"]} score={r.get("score")} seconds={r["seconds"]:.3f}', flush=True)
    export(output)


def export(output):
    rows = read_rows(output)
    fields = ["phase", "input", "variant", "seed", "iterations", "budget", "legal", "returncode",
              "score", "skew", "cost", "buffers", "seconds", "lp_calls", "lp_feasible", "lp_seconds",
              "repair_calls", "repair_feasible", "repair_seconds", "termination", "attempts", "error"]
    with (output / "runs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fields, extrasaction="ignore", lineterminator="\n");writer.writeheader()
        for row in sorted(rows, key=lambda r: (r['phase'], r['input'], r['variant'], r['seed'])):
            flat = dict(row)
            flat.update({k: v for k, v in row.get("statistics", {}).items() if k in fields})
            flat['attempts'] = sum(m['attempts'] for m in row.get('statistics', {}).get('moves', []))
            writer.writerow(flat)
    summary = {"runs": len(rows), "legal": sum(r['legal'] for r in rows),
               "counts": dict(Counter(r['phase'] for r in rows))}
    (output / "progress.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["prepare", "verify", "legacy", "screen", "confirm", "legacy_holdout", "init_stress", "export"])
    parser.add_argument("--work", type=pathlib.Path)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.phase == "prepare":
        if args.work is None:
            parser.error("prepare requires --work")
        prepare(args.work.resolve())
    elif args.phase == "export":
        if args.output is None:
            parser.error("export requires --output")
        export(args.output)
    else:
        if args.output is None:
            parser.error("experiments require --output")
        run_phase(args.root.resolve(), args.output.resolve(), args.phase)

if __name__ == "__main__":
    main()
