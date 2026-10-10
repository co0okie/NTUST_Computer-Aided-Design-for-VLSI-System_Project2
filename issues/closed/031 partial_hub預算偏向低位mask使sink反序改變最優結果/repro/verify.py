"""Independent witness/solver validation; does not use the grader."""
import pathlib,re,subprocess,sys
folder=pathlib.Path(__file__).resolve().parent
root=folder.parents[3]
binary=pathlib.Path(sys.argv[1]).resolve()
def check_output(text, pins, dim, source, library):
    lines = text.splitlines()
    count = int(lines[0].split()[1])
    positions = dict(zip(['SRC'] + [f'S{i}' for i in range(1, len(pins))], pins))
    limits = {'SRC': source}
    cost = 0
    for i, line in enumerate(lines[1:count + 1], 1):
        name, kind, x, y = line.split()
        assert name == f'B{i}'
        index = int(kind[1:])
        f, length, price = library[index]
        positions[name] = (int(x), int(y))
        assert 0 < int(x) < dim and 0 < int(y) < dim
        limits[name] = (f, length)
        cost += price
    assert len(set(positions.values())) == len(positions)
    assert lines[count + 1] == '.e'
    level_start = next(i for i, line in enumerate(lines) if line.startswith('.level '))
    declared_depth = int(lines[level_start].split()[1])
    edges = {}
    stated_depth = {}
    for line in lines[level_start + 1:-1]:
        level, groups = line.split(' ', 1)
        for name, contents in re.findall(r'(\S+):\{([^}]*)\}', groups):
            assert name in limits and name not in edges
            children = contents.split()
            assert children
            edges[name] = children
            for child in children:
                assert child in positions and child != 'SRC' and child not in stated_depth
                stated_depth[child] = int(level)
    assert lines[-1] == '.e'
    assert set(stated_depth) == set(positions) - {'SRC'}
    assert all(f'B{i}' in edges for i in range(1, count + 1))
    arrival = {'SRC': 0}
    depth = {'SRC': 0}
    queue = ['SRC']
    for parent in queue:
        children = edges.get(parent, [])
        if not children:
            assert parent.startswith('S')
            continue
        f, length = limits[parent]
        assert len(children) <= f
        outgoing = 0
        for child in children:
            assert child not in arrival
            a, b = positions[parent], positions[child]
            d = abs(a[0] - b[0]) + abs(a[1] - b[1])
            outgoing += d
            arrival[child] = arrival[parent] + d
            depth[child] = depth[parent] + 1
            assert stated_depth[child] == depth[child]
            queue.append(child)
        assert outgoing <= length
    assert set(queue) == set(positions)
    assert max(depth.values()) == declared_depth
    times = [arrival[f'S{i}'] for i in range(1, len(pins))]
    return max(times), min(times), cost

failed=False
for i in (1,2):
    text=(folder/f'input{i:03}.cbi').read_text()
    dim=int(re.search(r'\.dimx (\d+)',text)[1])
    source=tuple(map(int,re.search(r'fanout (\d+)\s+length (\d+)',text).groups()))
    library=tuple(tuple(map(int,x)) for x in re.findall(r'^T\d+ (\d+) (\d+) (\d+)$',text,re.M))
    pins=[tuple(map(int,line.split())) for line in re.search(r'\.pin \d+\n(.*?)\.e',text,re.S)[1].splitlines()]
    weight=int(re.search(r'w_skew (\d+)',text)[1])
    hi,lo,cost=check_output((folder/f'witness{i:03}.cbi').read_text(),pins,dim,source,library)
    print(i,'verified witness:',hi,lo,cost,'score',cost+weight*(hi-lo))
    output=root/f'tmp/tester/031/output{i:03}.cbi';output.parent.mkdir(parents=True,exist_ok=True)
    result=subprocess.run([str(binary),str(folder/f'input{i:03}.cbi'),str(output)],capture_output=True,text=True,timeout=30)
    if result.returncode:
        failed=True;print(i,'solver rejected:',result.returncode,result.stderr.strip());continue
    hi,lo,cost=check_output(output.read_text(),pins,dim,source,library)
    assert result.stdout.strip()==f'T_max: {hi}, T_min: {lo}, Score: {cost+weight*(hi-lo)}'
    score=cost+weight*(hi-lo)
    print(i,'verified solver:',hi,lo,cost,'score',score)
    if score>9:
        failed=True;print(i,'score exceeds witnessed optimum 9')
sys.exit(int(failed))
