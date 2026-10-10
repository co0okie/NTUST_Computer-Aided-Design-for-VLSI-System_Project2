#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib>=3.9,<4"]
# ///
"""Plot validated CBI input/solution pairs; batch normal and extreme cases.

uv run tools/plot_cbi.py plot example/input.cbi example/output1.cbi -o plot.svg
uv run tools/plot_cbi.py gallery --solver build/cbi --out-dir tmp/tester/plots

Net length means the SUM of a driver's outgoing Manhattan distances.
Sink delay means cumulative Manhattan distance from SRC (buffer delay is zero).
"""

from __future__ import annotations

import argparse
import colorsys
import html
import json
import math
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.lines import Line2D
from matplotlib.patches import FancyArrowPatch
from matplotlib.ticker import MaxNLocator


@dataclass(frozen=True)
class BufferType:
    name: str
    fanout: int
    length: int
    cost: int


@dataclass
class Problem:
    dimx: int
    dimy: int
    fanout: int
    length: int
    weight: int
    pins: list[tuple[int, int]]
    library: dict[str, BufferType]


@dataclass
class Tree:
    positions: dict[str, tuple[int, int]]
    types: dict[str, str]
    children: dict[str, list[str]]
    arrivals: dict[str, int]
    nets: dict[str, int]
    cost: int
    skew: int
    score: int


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def records(path: Path) -> list[str]:
    return [line for raw in path.read_text(encoding="utf-8-sig").splitlines()
            if (line := raw.split("#", 1)[0].strip())]


def sections(path: Path, lines: list[str] | None = None) -> dict[str, tuple[list[str], list[str]]]:
    result = {}
    name = None
    for line in records(path) if lines is None else lines:
        if line.startswith("."):
            words = line.split()
            if words[0] == ".e":
                require(name is not None and len(words) == 1, f"{path}: unexpected .e")
                name = None
            else:
                require(name is None, f"{path}: missing .e before {line}")
                name = words[0]
                require(name not in result, f"{path}: duplicate {name}")
                result[name] = (words[1:], [])
        else:
            require(name is not None, f"{path}: data outside a section")
            result[name][1].append(line)
    require(name is None, f"{path}: missing final .e")
    return result


def read_problem(path: Path) -> Problem:
    # .dimx/.dimy occur inside .limit rather than being standalone sections.
    cleaned = [line[1:] if line.split()[0] in {".dimx", ".dimy"} else line for line in records(path)]
    blocks = sections(path, cleaned)
    require(set(blocks) == {".limit", ".buflib", ".objective", ".pin"}, "unexpected input sections")
    require(not blocks[".limit"][0] and not blocks[".objective"][0], "unexpected section arguments")
    require(len(blocks[".buflib"][0]) == len(blocks[".pin"][0]) == 1, "missing section count")
    require(len(blocks[".limit"][1]) == 4 and len(blocks[".objective"][1]) == 1, "missing/duplicate limit fields")
    limits = dict(line.split() for line in blocks[".limit"][1])
    objective = dict(line.split() for line in blocks[".objective"][1])
    require(set(limits) == {"fanout", "length", "dimx", "dimy"} and set(objective) == {"w_skew"}, "bad limits/objective")
    library = {}
    for line in blocks[".buflib"][1]:
        kind, fanout, length, cost = line.split()
        require(kind not in library, f"duplicate buffer type {kind}")
        library[kind] = BufferType(kind, int(fanout), int(length), int(cost))
    require(len(library) == int(blocks[".buflib"][0][0]), "library count mismatch")
    pins = [tuple(map(int, line.split())) for line in blocks[".pin"][1]]
    require(len(pins) == int(blocks[".pin"][0][0]) and len(pins) >= 2, "pin count mismatch")
    p = Problem(int(limits["dimx"]), int(limits["dimy"]), int(limits["fanout"]), int(limits["length"]),
                int(objective["w_skew"]), pins, library)
    require(min(p.dimx, p.dimy, p.fanout, p.length, p.weight) > 0, "limits must be positive")
    require(bool(library), "empty library")
    require(all(min(t.fanout, t.length, t.cost) > 0 for t in library.values()), "buffer limits must be positive")
    require(all(len(pos) == 2 and 0 <= pos[0] <= p.dimx and 0 <= pos[1] <= p.dimy for pos in pins), "pin outside chip")
    require(len(set(pins)) == len(pins), "overlapping pins")
    return p


def manhattan(a: tuple[int, int], b: tuple[int, int]) -> int:
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def read_tree(path: Path, p: Problem) -> Tree:
    blocks = sections(path)
    require(set(blocks) == {".buffer", ".level"}, "unexpected output sections")
    require(len(blocks[".buffer"][0]) == len(blocks[".level"][0]) == 1, "missing section count")
    positions = dict(zip(["SRC"] + [f"S{i}" for i in range(1, len(p.pins))], p.pins))
    types = {}
    for i, line in enumerate(blocks[".buffer"][1], 1):
        name, kind, x, y = line.split()
        require(name == f"B{i}" and kind in p.library, f"bad buffer {line}")
        pos = (int(x), int(y))
        require(0 < pos[0] < p.dimx and 0 < pos[1] < p.dimy, f"{name}: buffer must be strictly interior")
        positions[name], types[name] = pos, kind
    require(len(types) == int(blocks[".buffer"][0][0]), "buffer count mismatch")
    require(len(set(positions.values())) == len(positions), "overlapping components")
    children, stated = {}, {}
    for line in blocks[".level"][1]:
        level, groups = line.split(maxsplit=1)
        matches = list(re.finditer(r"(\w+)\s*:\s*\{([^{}]*)\}", groups))
        require(bool(matches) and not re.sub(r"\w+\s*:\s*\{[^{}]*\}", "", groups).strip(), "bad level syntax")
        for match in matches:
            parent, names = match[1], match[2].split()
            require(parent in {"SRC", *types} and parent not in children and bool(names), f"bad driver {parent}")
            children[parent] = names
            for child in names:
                require(child in positions and child != "SRC" and child not in stated, f"bad/repeated child {child}")
                stated[child] = int(level)
    require(set(stated) == set(positions) - {"SRC"}, "unconnected component")
    require(set(children) == {"SRC", *types}, "each root/buffer must drive a nonempty net")
    arrival, depth, nets = {"SRC": 0}, {"SRC": 0}, {}
    queue = ["SRC"]
    for parent in queue:
        if parent not in children:
            continue
        names = children[parent]
        fanout, length = (p.fanout, p.length) if parent == "SRC" else (p.library[types[parent]].fanout, p.library[types[parent]].length)
        require(len(names) <= fanout, f"{parent}: fanout exceeded")
        nets[parent] = sum(manhattan(positions[parent], positions[child]) for child in names)
        require(nets[parent] <= length, f"{parent}: total outgoing length exceeded")
        for child in names:
            require(child not in arrival, "cycle in tree")
            arrival[child] = arrival[parent] + manhattan(positions[parent], positions[child])
            depth[child] = depth[parent] + 1
            require(stated[child] == depth[child], f"{child}: incorrect .level")
            queue.append(child)
    require(len(arrival) == len(positions), "cycle or disconnected subtree")
    require(max(depth.values()) == int(blocks[".level"][0][0]), "incorrect maximum depth")
    times = [arrival[f"S{i}"] for i in range(1, len(p.pins))]
    cost = sum(p.library[kind].cost for kind in types.values())
    skew = max(times) - min(times)
    return Tree(positions, types, children, arrival, nets, cost, skew, cost + p.weight * skew)


def configure_font(path: Path | None) -> None:
    if path:
        font_manager.fontManager.addfont(str(path))
        family = font_manager.FontProperties(fname=path).get_name()
    else:
        family = "DejaVu Sans"
    plt.rcParams.update({"font.family": [family, "DejaVu Sans"], "axes.unicode_minus": False,
                         "svg.fonttype": "path"})


def place_labels(ax, specs, size):
    """Place nearby labels with minimal overlap using spatial bins."""
    fig = ax.figure
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()
    occupied = {}
    boxes = []
    cell = 48

    def cells(box):
        return [(x, y) for x in range(math.floor(box.x0 / cell), math.floor(box.x1 / cell) + 1)
                for y in range(math.floor(box.y0 / cell), math.floor(box.y1 / cell) + 1)]

    for text, xy, color, net in specs:
        offsets = [(9, 9), (9, -9), (-9, 9), (-9, -9), (0, 16), (0, -16), (18, 0), (-18, 0)]
        if net:
            offsets = [(0, 7), (0, -7), (12, 10), (-12, -10)] + offsets
        annotation = ax.annotate(text, xy, xytext=offsets[0], textcoords="offset points", fontsize=size,
                                 color=color, zorder=5, annotation_clip=False,
                                 bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.88, "pad": 0.7})
        candidates = []
        for dx, dy in offsets:
            annotation.set_position((dx, dy))
            annotation.set_ha("left" if dx > 0 else "right" if dx < 0 else "center")
            annotation.set_va("bottom" if dy > 0 else "top" if dy < 0 else "center")
            box = annotation.get_window_extent(renderer).expanded(1.06, 1.1)
            nearby = set().union(*(occupied.get(key, set()) for key in cells(box)))
            overlaps = sum(box.overlaps(boxes[i]) for i in nearby)
            outside = int(not ax.bbox.contains(box.x0, box.y0)) + int(not ax.bbox.contains(box.x1, box.y1))
            candidates.append((overlaps * 10 + outside, abs(dx) + abs(dy), dx, dy, box))
        _, _, dx, dy, box = min(candidates, key=lambda item: item[:2])
        annotation.set_position((dx, dy))
        annotation.set_ha("left" if dx > 0 else "right" if dx < 0 else "center")
        annotation.set_va("bottom" if dy > 0 else "top" if dy < 0 else "center")
        for key in cells(box):
            occupied.setdefault(key, set()).add(len(boxes))
        boxes.append(box)


def draw(p: Problem, tree: Tree, destination: Path, args) -> None:
    require(destination.suffix.lower() == ".svg", "output must be SVG")
    count = len(tree.positions) + len(tree.nets)
    side = args.size or max(9, min(38, math.sqrt(count) * 0.72))
    width = side * p.dimx / max(p.dimx, p.dimy)
    height = side * p.dimy / max(p.dimx, p.dimy)
    columns = max(1, math.ceil((len(p.library) + 2) / 24))
    fig, ax = plt.subplots(figsize=(max(5, width) + columns * 3.6, max(5, height) + 1.4))
    fig.subplots_adjust(left=0.08, right=max(5, width) / (max(5, width) + columns * 3.6), top=0.86, bottom=0.11)
    ax.set(xlim=(0, p.dimx), ylim=(0, p.dimy), xlabel="x", ylabel="y")
    ax.set_aspect("equal", adjustable="box")
    ax.xaxis.set_major_locator(MaxNLocator(nbins=8, integer=True))
    ax.yaxis.set_major_locator(MaxNLocator(nbins=8, integer=True))
    for axis, limit in ((ax.xaxis, p.dimx), (ax.yaxis, p.dimy)):
        ticks = [tick for tick in axis.get_majorticklocs() if 0 < tick < limit and limit - tick > limit / 30]
        axis.set_ticks([0, *ticks, limit])
    ax.grid(color="#dddddd", linewidth=0.6, zorder=0)
    ax.set_title(f"{tree.score} = {tree.cost} + {p.weight} × {tree.skew}", fontsize=13, pad=15)
    colors = {kind: colorsys.hsv_to_rgb((i * 0.61803398875) % 1, 0.7, 0.72)
              for i, kind in enumerate(p.library)}
    specs = []
    for parent, children in tree.children.items():
        color = "#222222" if parent == "SRC" else colors[tree.types[parent]]
        start = tree.positions[parent]
        for child in children:
            ax.add_patch(FancyArrowPatch(start, tree.positions[child], arrowstyle="-|>",
                                        connectionstyle="arc3,rad=0", mutation_scale=9,
                                        shrinkA=7 if parent == "SRC" else 4, shrinkB=4,
                                        color=color, linewidth=0.8, alpha=0.75, zorder=1))
        end = tree.positions[children[0]]
        anchor = (start[0] * 0.65 + end[0] * 0.35, start[1] * 0.65 + end[1] * 0.35)
        specs.append((f"ΣL={tree.nets[parent]}", anchor, color, True))
    for kind in p.library:
        points = [tree.positions[name] for name, value in tree.types.items() if value == kind]
        if points:
            ax.scatter(*zip(*points), marker="s", c=[colors[kind]], s=28, edgecolors="black", linewidths=0.4, zorder=4)
    sinks = [tree.positions[f"S{i}"] for i in range(1, len(p.pins))]
    ax.scatter(*zip(*sinks), marker="o", s=25, facecolors="white", edgecolors="#185a9d", linewidths=0.8, zorder=4)
    ax.scatter(*tree.positions["SRC"], marker="*", s=150, color="#222222", zorder=6)
    specs = [(f"delay={tree.arrivals[name]}", tree.positions[name], "#185a9d", False)
             for name in tree.positions if name.startswith("S") and name != "SRC"] + specs
    handles = [Line2D([], [], marker="*", color="#222222", linestyle="none", markersize=11,
                      label=f"root: fanout={p.fanout}, length={p.length}"),
               Line2D([], [], marker="o", color="#185a9d", markerfacecolor="white", linestyle="none", label="sink")]
    for kind, spec in p.library.items():
        handles.append(Line2D([], [], marker="s", color=colors[kind], markeredgecolor="black", linestyle="none",
                              label=f"fanout={spec.fanout}, length={spec.length}, cost={spec.cost}"))
    ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.02, 1), ncols=columns, fontsize=8, frameon=True)
    size = args.font_size or max(4.5, min(9, 11 - math.log10(count + 1) * 1.7))
    place_labels(ax, specs, size)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(destination, format="svg", bbox_inches="tight", facecolor="white")
    plt.close(fig)


def demo_inputs(folder: Path) -> list[tuple[str, Path]]:
    cases = [
        ("single_sink", 10, 10, (1, 1), [(5, 5), (5, 6)], [(1, 1, 1)], 100),
        ("zero_buffers_saturated_root", 24, 10, (3, 6), [(12, 5), (10, 5), (14, 5), (12, 7)], [(2, 4, 7), (4, 20, 9)], 9),
        ("thin_chip_long_relay", 160, 12, (1, 40), [(0, 0), (160, 12)], [(1, 40, 1), (1, 100, 100)], 100),
        ("rectangular_boundary", 60, 20, (2, 50), [(0, 10), (60, 10), (0, 0), (60, 20)], [(3, 160, 1)], 10),
        ("tiny_boundary_direct", 3, 2, (1, 5), [(0, 0), (3, 2)], [(1, 2, 1)], 100),
        ("delay_zero_skew", 10, 10, (2, 100), [(5, 5), (7, 5), (9, 5)], [(1, 100, 1)], 100),
    ]
    folder.mkdir(parents=True, exist_ok=True)
    result = []
    for name, dx, dy, limits, pins, library, weight in cases:
        text = [f".limit\nfanout {limits[0]}\nlength {limits[1]}\n.dimx {dx}\n.dimy {dy}\n.e", f".buflib {len(library)}"]
        text += [f"T{i} {fanout} {length} {cost}" for i, (fanout, length, cost) in enumerate(library)]
        text += [f".e\n.objective\nw_skew {weight}\n.e\n.pin {len(pins)}"]
        text += [f"{x} {y}" for x, y in pins] + [".e\n"]
        path = folder / f"{name}.cbi"
        path.write_text("\n".join(text), encoding="utf-8")
        result.append((name, path))
    return result


def gallery(args, root: Path) -> None:
    solver = (args.solver or root / "build/cbi").resolve()
    require(solver.is_file(), f"solver not found: {solver}; supply --solver")
    folder = args.out_dir.resolve()
    require(not folder.exists() or folder.is_dir(), "gallery destination must be a directory")
    folder.mkdir(parents=True, exist_ok=True)
    if args.case_dir:
        cases = [(f"{i:03}_{p.stem}", p) for i, p in enumerate(sorted(args.case_dir.rglob("*.cbi")), 1)
                 if any(line.startswith(".limit") for line in records(p))]
    else:
        cases = [("example", root / "example/input.cbi")]
        for path in sorted((root / "issues/closed").rglob("input*.cbi")):
            issue = next(part for part in path.parts if re.match(r"^\d{3} ", part))
            cases.append((f"issue_{issue[:3]}_{path.stem}", path))
        cases += demo_inputs(folder / "demo_inputs")
    require(bool(cases), "no inputs found")
    report, errors = [], []
    for name, source in cases:
        try:
            p = read_problem(source)
            output = folder / f"{name}.cbi"
            require(source.resolve() != output.resolve(), "refusing to overwrite input")
            completed = subprocess.run([str(solver), str(source.resolve()), str(output)], capture_output=True, text=True, timeout=60)
            require(completed.returncode == 0, completed.stderr.strip() or f"solver exit {completed.returncode}")
            tree = read_tree(output, p)
            draw(p, tree, folder / f"{name}.svg", args)
            report.append({"case": name, "input": str(source.resolve()), "output": str(output),
                           "score": tree.score, "buffer_cost": tree.cost, "w": p.weight, "max_skew": tree.skew,
                           "buffers": len(tree.types), "sinks": len(p.pins) - 1,
                           "net_lengths": tree.nets, "sink_delays": {name: value for name, value in tree.arrivals.items() if name.startswith("S") and name != "SRC"}})
            print(f"{name}: score={tree.score}, buffers={len(tree.types)}, sinks={len(p.pins)-1}", flush=True)
        except (ValueError, KeyError, OSError, subprocess.TimeoutExpired) as exc:
            errors.append({"case": name, "error": str(exc)})
            print(f"{name}: {exc}", file=sys.stderr)
    (folder / "metrics.json").write_text(json.dumps({"cases": report, "errors": errors}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    cards = "\n".join(f'<article><h2>{html.escape(item["case"])}</h2><a href="{html.escape(item["case"])}.svg"><img loading="lazy" src="{html.escape(item["case"])}.svg" alt="{html.escape(item["case"])}"></a></article>' for item in report)
    (folder / "index.html").write_text('<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>CBI plots</title><style>body{margin:2rem;font-family:sans-serif}article{margin:2rem 0}img{max-width:100%;height:auto}h2{font-size:1rem}</style><h1>CBI plots</h1><p>點選圖片開啟可放大的 SVG。數值與來源記錄在 metrics.json。</p>' + cards + "</html>", encoding="utf-8")
    require(not errors, f"{len(errors)} case(s) failed; see {folder / 'metrics.json'}")
    print(f"{len(report)} valid cases → {folder / 'index.html'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    single = commands.add_parser("plot", help="validate and draw one input/solution pair")
    single.add_argument("input", type=Path)
    single.add_argument("solution", type=Path)
    single.add_argument("-o", "--output", type=Path, required=True)
    batch = commands.add_parser("gallery", help="solve and draw example, closed issues and extra extreme cases")
    batch.add_argument("--solver", type=Path)
    batch.add_argument("--case-dir", type=Path, help="use this input directory instead of the built-in case collection")
    batch.add_argument("--out-dir", type=Path, default=Path("tmp/tester/plots"))
    for command in (single, batch):
        command.add_argument("--font", type=Path, help="optional TTF/OTF/TTC font")
        command.add_argument("--size", type=float, help="long side of the chip in inches; default scales with node count")
        command.add_argument("--font-size", type=float, help="node/net label size in points")
    args = parser.parse_args()
    try:
        require((args.size is None or args.size > 0) and (args.font_size is None or args.font_size > 0), "sizes must be positive")
        configure_font(args.font)
        if args.command == "plot":
            require(args.output.resolve() not in {args.input.resolve(), args.solution.resolve()}, "refusing to overwrite input/solution")
            p = read_problem(args.input)
            tree = read_tree(args.solution, p)
            draw(p, tree, args.output, args)
            print(f"{args.output}: score={tree.score}, cost={tree.cost}, skew={tree.skew}")
        else:
            gallery(args, Path(__file__).resolve().parents[1])
        return 0
    except (ValueError, KeyError, IndexError, OSError) as exc:
        print(f"plot_cbi: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
