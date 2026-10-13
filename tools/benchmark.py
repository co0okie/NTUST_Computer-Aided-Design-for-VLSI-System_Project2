#!/usr/bin/env python3
"""Compare solver strategies with an independent integer verifier; never runs the grader."""
import argparse
import concurrent.futures
import csv
import hashlib
import platform
import shutil
import json
import pathlib
import random
import re
import subprocess
import time


def read_problem(path):
    lines = [line.split('#', 1)[0].split() for line in path.read_text().splitlines()]
    lines = [line for line in lines if line]
    limits = dict((line[0], int(line[1])) for line in lines[1:5])
    index = next(i for i, line in enumerate(lines) if line[0] == '.buflib')
    library = {line[0]: tuple(map(int, line[1:])) for line in lines[index+1:index+1+int(lines[index][1])]}
    weight = next(int(line[1]) for line in lines if line[0] == 'w_skew')
    index = next(i for i, line in enumerate(lines) if line[0] == '.pin')
    pins = [tuple(map(int, line)) for line in lines[index+1:index+1+int(lines[index][1])]]
    return limits, library, weight, pins


def verify(problem, output):
    limits, library, weight, pins = problem
    lines = [line.split('#', 1)[0].strip() for line in output.splitlines()]
    lines = [line for line in lines if line]
    assert lines[0].startswith('.buffer ')
    count = int(lines[0].split()[1])
    positions = dict(zip(['SRC'] + [f'S{i}' for i in range(1, len(pins))], pins))
    drivers = {'SRC': (limits['fanout'], limits['length'])}
    cost = 0
    for i, line in enumerate(lines[1:count+1], 1):
        name, kind, x, y = line.split()
        assert name == f'B{i}' and kind in library
        point = int(x), int(y)
        assert 0 < point[0] < limits['.dimx'] and 0 < point[1] < limits['.dimy']
        positions[name] = point
        f, length, price = library[kind]
        drivers[name] = f, length
        cost += price
    assert len(set(positions.values())) == len(positions)
    assert lines[count+1] == '.e' and lines[count+2].startswith('.level ')
    expected_depth = int(lines[count+2].split()[1])
    edges, levels = {}, {}
    for line in lines[count+3:-1]:
        level, groups = line.split(' ', 1)
        matches = list(re.finditer(r'(\S+):\{([^}]*)\}', groups))
        assert re.sub(r'(\S+):\{([^}]*)\}', '', groups).strip() == ''
        for match in matches:
            parent, children = match.group(1), match.group(2).split()
            assert parent in drivers and parent not in edges and children
            edges[parent] = children
            for child in children:
                assert child in positions and child != 'SRC' and child not in levels
                levels[child] = int(level)
    assert lines[-1] == '.e'
    assert set(levels) == set(positions) - {'SRC'}
    assert set(edges) == set(drivers)
    arrivals, depths, queue = {'SRC': 0}, {'SRC': 0}, ['SRC']
    for parent in queue:
        children = edges.get(parent, [])
        if not children:
            assert parent.startswith('S')
            continue
        f, length = drivers[parent]
        assert len(children) <= f
        wire = 0
        for child in children:
            assert child not in arrivals
            a, b = positions[parent], positions[child]
            distance = abs(a[0]-b[0]) + abs(a[1]-b[1])
            wire += distance
            arrivals[child] = arrivals[parent] + distance
            depths[child] = depths[parent] + 1
            assert levels[child] == depths[child]
            queue.append(child)
        assert wire <= length
    assert set(queue) == set(positions) and max(depths.values()) == expected_depth
    values = [arrivals[f'S{i}'] for i in range(1, len(pins))]
    skew = max(values) - min(values)
    return {'score': cost + weight*skew, 'skew': skew, 'cost': cost, 'buffers': count}


def generate(folder, count, seed):
    folder.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    files = []
    for i in range(count):
        dimension = rng.randrange(8, 81)
        pins = rng.sample([(x,y) for x in range(dimension+1) for y in range(dimension+1)], rng.randrange(3, 23))
        types = [(rng.randrange(2,9), rng.randrange(10,dimension*5), rng.randrange(1,16)) for _ in range(rng.randrange(1,5))]
        source = rng.randrange(1,6), rng.randrange(10,dimension*5)
        weight = rng.choice([1,5,100])
        text = f'.limit\nfanout {source[0]}\nlength {source[1]}\n.dimx {dimension}\n.dimy {dimension}\n.e\n.buflib {len(types)}\n'
        text += ''.join(f'T{j} {f} {length} {cost}\n' for j,(f,length,cost) in enumerate(types))
        text += f'.e\n.objective\nw_skew {weight}\n.e\n.pin {len(pins)}\n' + ''.join(f'{x} {y}\n' for x,y in pins) + '.e\n'
        path = folder/f'input{i:03}.cbi'
        path.write_text(text)
        files.append(path)
    return files


def run_case(binary, path, strategy, seed, budget, destination, extra):
    problem = read_problem(path)
    output = destination.with_suffix('.cbi')
    stats = destination.with_suffix('.json')
    memory = destination.with_suffix('.rss')
    command = [str(binary), str(path), str(output), '--strategy', strategy, '--time', str(budget), '--seed', str(seed), '--stats', str(stats)] + extra
    start = time.monotonic()
    try:
        run = subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(memory)] + command, text=True, capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        return {'input': str(path), 'strategy': strategy, 'seed': seed, 'budget': budget, 'legal': False, 'error': 'timeout', 'seconds': time.monotonic()-start}
    row = {'input': str(path), 'strategy': strategy, 'seed': seed, 'budget': budget, 'legal': False, 'seconds': time.monotonic()-start, 'returncode': run.returncode}
    if memory.exists():
        values = memory.read_text().splitlines()
        row['rss_kib'] = int(values[-1]) if values and values[-1].isdigit() else None
    if run.returncode == 0:
        try:
            row.update(verify(problem, output.read_text()))
            row['legal'] = True
            row['statistics'] = json.loads(stats.read_text())
            expected = row['statistics']
            assert expected['score'] == row['score'] and expected['skew'] == row['skew']
            assert re.search(rf'Score: {row["score"]}\s*$', run.stdout)
        except (AssertionError, ValueError, KeyError) as error:
            row['legal'] = False
            row['error'] = f'independent validation failed: {error}'
    else:
        row['error'] = run.stderr
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--inputs', type=pathlib.Path, nargs='*', default=[])
    parser.add_argument('--issues', action='store_true')
    parser.add_argument('--random', type=int, default=0)
    parser.add_argument('--generation-seed', type=int, default=20261013)
    parser.add_argument('--seeds', type=int, nargs='+', default=[1,2,3,4,5])
    parser.add_argument('--budgets', type=float, nargs='+', default=[55])
    parser.add_argument('--strategies', nargs='+', choices=['current','sa_lp','hybrid'], default=['current','sa_lp','hybrid'])
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--extra', nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    inputs = args.inputs + generate(args.output/'inputs', args.random, args.generation_seed)
    if args.issues:
        inputs += sorted(pathlib.Path('issues').rglob('input*.cbi'))
    # Freeze the executable so rebuilding the development target cannot corrupt a run.
    binary = args.output / 'cbi'
    shutil.copy2(args.binary.resolve(), binary)
    metadata = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'platform': platform.platform(), 'jobs': args.jobs, 'budgets': args.budgets,
                'seeds': args.seeds, 'extra': args.extra, 'inputs': list(map(str, inputs))}
    (args.output/'metadata.json').write_text(json.dumps(metadata, indent=2))
    tasks = []
    for path in inputs:
        for budget in args.budgets:
            for strategy in args.strategies:
                for seed in ([args.seeds[0]] if strategy == 'current' else args.seeds):
                    tasks.append((binary.resolve(),path,strategy,seed,budget,args.output/f'run{len(tasks):05}',args.extra))
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(run_case,*task) for task in tasks]
        for future in concurrent.futures.as_completed(futures):
            row = future.result(); rows.append(row)
            print(f'{len(rows)}/{len(tasks)} {row["strategy"]} {row["input"]}: {row.get("score", "failed")}', flush=True)
            (args.output/'results.json').write_text(json.dumps(rows,indent=2))
    keys = ['input','strategy','seed','budget','legal','score','skew','cost','buffers','seconds','rss_kib','error']
    with (args.output/'results.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=keys,extrasaction='ignore'); writer.writeheader(); writer.writerows(rows)


if __name__ == '__main__':
    main()
