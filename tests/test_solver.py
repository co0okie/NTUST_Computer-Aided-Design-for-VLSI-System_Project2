#!/usr/bin/env python3
"""Independent end-to-end checks; does not import or execute the grader."""
import pathlib
import random
import re
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
ROOT = pathlib.Path(__file__).resolve().parents[1]


def instance(pins, dim=100, source=(2, 100), library=((3, 100, 10),)):
    lines = [f'.limit\nfanout {source[0]}\nlength {source[1]}',
             f'.dimx {dim}\n.dimy {dim}\n.e\n.buflib {len(library)}']
    lines += [f'T{i} {f} {length} {cost}' for i, (f, length, cost) in enumerate(library)]
    lines += ['.e\n.objective\nw_skew 100\n.e', f'.pin {len(pins)}']
    lines += [f'{x} {y}' for x, y in pins]
    return '\n'.join(lines + ['.e\n'])


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


class SolverTests(unittest.TestCase):
    def run_case(self, pins, dim=100, source=(2, 100), library=((3, 100, 10),)):
        with tempfile.TemporaryDirectory() as folder:
            inp, out = pathlib.Path(folder) / 'input001.cbi', pathlib.Path(folder) / 'output001.cbi'
            inp.write_text(instance(pins, dim, source, library))
            result = subprocess.run([str(BINARY), str(inp), str(out)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            maximum, minimum, cost = check_output(out.read_text(), pins, dim, source, library)
            self.assertEqual(result.stdout.strip(),
                             f'T_max: {maximum}, T_min: {minimum}, Score: {100 * (maximum - minimum) + cost}')
            return maximum - minimum, out.read_text()

    def test_direct(self):
        skew, text = self.run_case([(10, 10), (20, 10), (10, 20)])
        self.assertEqual(skew, 0)
        self.assertTrue(text.startswith('.buffer 0\n'))

    def test_zero_skew(self):
        skew, _ = self.run_case([(97, 38), (49, 61), (17, 25), (56, 82), (72, 26), (9, 91)],
                                dim=110, source=(2, 100), library=((3, 90, 10), (6, 120, 16), (9, 190, 22)))
        self.assertEqual(skew, 0)

    def test_mixed_parity(self):
        skew, _ = self.run_case([(97, 38), (49, 62), (17, 25), (56, 82), (72, 27), (9, 91)],
                                dim=110, source=(2, 100), library=((3, 90, 10), (6, 120, 16), (9, 190, 22)))
        self.assertEqual(skew, 1)

    def test_relays(self):
        self.run_case([(1, 1), (80, 80), (80, 20), (20, 80), (20, 20)],
                      source=(1, 7), library=((2, 12, 2), (4, 18, 5)))

    def test_boundary_sink(self):
        self.run_case([(0, 0), (100, 100)], source=(1, 5), library=((1, 9, 1),))

    def test_unary_library_with_source_branching(self):
        skew, _ = self.run_case([(50, 50), (10, 10), (90, 90)],
                                source=(2, 10), library=((1, 15, 1),))
        self.assertEqual(skew, 0)

    def test_same_boundary_relay(self):
        # Each edge's shortest path lies on a forbidden buffer boundary.
        for pins in [[(10, 0), (90, 0)], [(10, 100), (90, 100)],
                     [(0, 10), (0, 90)], [(100, 10), (100, 90)],
                     [(0, 0), (100, 0)]]:
            skew, _ = self.run_case(pins, source=(1, 5), library=((1, 10, 1),))
            self.assertEqual(skew, 0)

    def test_random_sparse(self):
        rng = random.Random(20261009)
        for _ in range(40):
            pins = rng.sample([(x, y) for x in range(5, 96, 5) for y in range(5, 96, 5)], 21)
            self.run_case(pins, source=(2, 30), library=((2, 30, 2), (6, 80, 5)))

    def test_reject_without_overwriting(self):
        good = instance([(10, 10), (20, 20)])
        bad_cases = [good.replace('20 20', '10 10'), good.replace('20 20', '101 20'),
                     good.replace('fanout 2', 'fanout -2'), good.replace('w_skew 100', 'w_skew nope'),
                     good + 'unexpected\n', good.replace('.pin 2', '.pin 3'),
                     instance([(0, 0), (2, 2)], dim=2, source=(1, 1), library=((1, 1, 1),))]
        with tempfile.TemporaryDirectory() as folder:
            inp, out = pathlib.Path(folder) / 'in', pathlib.Path(folder) / 'out'
            for bad in bad_cases:
                inp.write_text(bad)
                out.write_text('preserve existing output')
                result = subprocess.run([str(BINARY), str(inp), str(out)], capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(out.read_text(), 'preserve existing output')

    def test_missing_arguments(self):
        result = subprocess.run([str(BINARY)], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b'Usage:', result.stderr)


if __name__ == '__main__':
    unittest.main()
