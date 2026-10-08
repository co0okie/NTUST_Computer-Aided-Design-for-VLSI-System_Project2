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


def instance(pins, dim=100, source=(2, 100), library=((3, 100, 10),), weight=100):
    lines = [f'.limit\nfanout {source[0]}\nlength {source[1]}',
             f'.dimx {dim}\n.dimy {dim}\n.e\n.buflib {len(library)}']
    lines += [f'T{i} {f} {length} {cost}' for i, (f, length, cost) in enumerate(library)]
    lines += [f'.e\n.objective\nw_skew {weight}\n.e', f'.pin {len(pins)}']
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
    def run_case(self, pins, dim=100, source=(2, 100), library=((3, 100, 10),), weight=100):
        with tempfile.TemporaryDirectory() as folder:
            inp, out = pathlib.Path(folder) / 'input001.cbi', pathlib.Path(folder) / 'output001.cbi'
            inp.write_text(instance(pins, dim, source, library, weight))
            result = subprocess.run([str(BINARY), str(inp), str(out)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            maximum, minimum, cost = check_output(out.read_text(), pins, dim, source, library)
            self.assertEqual(result.stdout.strip(),
                             f'T_max: {maximum}, T_min: {minimum}, Score: {weight * (maximum - minimum) + cost}')
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

    def test_source_budget_with_occupied_launch(self):
        for sinks in [[(9, 5), (6, 5)], [(6, 5), (9, 5)]]:
            _, text = self.run_case([(5, 5)] + sinks, dim=10,
                                    source=(2, 3), library=((1, 3, 1),))
            self.assertTrue(text.startswith('.buffer 1\n'))

    def test_multi_output_hub(self):
        pins = [(5, 1), (4, 5), (5, 6), (6, 5), (5, 4)]
        skew, text = self.run_case(pins, dim=10, source=(1, 4), library=((4, 20, 10),))
        self.assertEqual(skew, 0)
        self.assertTrue(text.startswith('.buffer 1\n'))
        # A single hub cannot satisfy either of these fanout/total-length limits.
        for library in [((2, 20, 10),), ((4, 3, 10),)]:
            _, text = self.run_case(pins, dim=10, source=(1, 4), library=library)
            self.assertGreater(int(text.splitlines()[0].split()[1]), 1)

    def test_hub_includes_source_relay_cost(self):
        pins = [(6, 1), (3, 1), (7, 9), (7, 1)]
        skew, text = self.run_case(pins, dim=10, source=(1, 1), library=((3, 30, 10),), weight=1)
        _, _, cost = check_output(text, pins, 10, (1, 1), ((3, 30, 10),))
        self.assertLessEqual(skew + cost, 16)

    def test_hub_multiple_relays_and_type_reselection(self):
        pins = [(2, 2), (70, 50), (72, 50), (71, 51)]
        library = ((3, 14, 10), (1, 14, 1))
        skew, text = self.run_case(pins, source=(1, 1), library=library, weight=1)
        _, _, cost = check_output(text, pins, 100, (1, 1), library)
        count = int(text.splitlines()[0].split()[1])
        types = [line.split()[1] for line in text.splitlines()[1:count + 1]]
        self.assertGreater(count, 3)
        self.assertEqual(types.count('T0'), 1)
        self.assertEqual(types.count('T1'), count - 1)
        self.assertEqual(cost, 10 + count - 1)
        self.assertEqual(skew + cost, 19)

    def test_hub_fallback_after_unreachable_candidate(self):
        # Low-skew hubs below SRC cannot pass its three occupied neighbors.
        # An independent trial above SRC can connect directly and remain legal.
        pins = [(5, 5), (4, 5), (6, 5), (5, 4), (5, 1)]
        skew, text = self.run_case(pins, dim=10, source=(1, 1), library=((4, 30, 10),), weight=1)
        _, _, cost = check_output(text, pins, 10, (1, 1), ((4, 30, 10),))
        self.assertEqual(skew + cost, 13)
        self.assertTrue(text.startswith('.buffer 1\n'))

    def test_relay_spacing_cost_tradeoff(self):
        pins = [(2, 2), (32, 2)]
        for library, expected, buffers in [(((1, 5, 1), (1, 100, 100)), 6, 6),
                                            (((1, 5, 1), (1, 100, 4)), 4, 1)]:
            skew, text = self.run_case(pins, dim=40, source=(1, 1), library=library, weight=1)
            maximum, minimum, cost = check_output(text, pins, 40, (1, 1), library)
            self.assertEqual((maximum, minimum), (30, 30))
            self.assertEqual(skew + cost, expected)
            self.assertEqual(int(text.splitlines()[0].split()[1]), buffers)

    def test_relay_short_final_segment(self):
        pins = [(2, 2), (14, 2)]
        library = ((1, 5, 1), (1, 100, 100))
        _, text = self.run_case(pins, dim=40, source=(1, 1), library=library, weight=1)
        maximum, minimum, cost = check_output(text, pins, 40, (1, 1), library)
        self.assertEqual((maximum, minimum, cost), (12, 12, 3))

    def test_relay_spacing_with_occupied_pin(self):
        pins = [(2, 2), (32, 2), (8, 2)]
        library = ((1, 5, 1), (1, 100, 100))
        skew, text = self.run_case(pins, dim=40, source=(2, 2), library=library, weight=1)
        _, _, cost = check_output(text, pins, 40, (2, 2), library)
        self.assertLessEqual(skew + cost, 34)

    def test_mixed_relay_types_and_order(self):
        for library in [((1, 8, 5), (1, 6, 4)), ((1, 6, 4), (1, 8, 5))]:
            for pins in [[(2, 2), (23, 2)], [(23, 2), (2, 2)]]:
                _, text = self.run_case(pins, dim=40, source=(1, 1), library=library, weight=1)
                maximum, minimum, cost = check_output(text, pins, 40, (1, 1), library)
                self.assertEqual((maximum, minimum, cost), (21, 21, 13))

    def test_mixed_relay_short_tail(self):
        pins = [(2, 2), (22, 2)]
        library = ((1, 8, 5), (1, 6, 4))
        _, text = self.run_case(pins, dim=40, source=(1, 1), library=library, weight=1)
        self.assertEqual(check_output(text, pins, 40, (1, 1), library), (20, 20, 13))

    def test_mixed_relay_occupied_launch(self):
        pins = [(2, 2), (23, 2), (11, 2)]
        library = ((1, 8, 5), (1, 6, 4))
        skew, text = self.run_case(pins, dim=40, source=(2, 18), library=library, weight=1)
        _, _, cost = check_output(text, pins, 40, (2, 18), library)
        self.assertLessEqual(skew + cost, 21)

    def test_large_distance_mixed_relay_fallback(self):
        # Cheap short chains exceed the component budget; no distance-sized DP
        # allocation is allowed. The long-type candidate remains feasible.
        pins = [(2, 2), (10**12 + 2, 2)]
        library = ((1, 10**12, 5), (1, 6, 4))
        _, text = self.run_case(pins, dim=2 * 10**12, source=(1, 1), library=library, weight=1)
        self.assertEqual(check_output(text, pins, 2 * 10**12, (1, 1), library), (10**12, 10**12, 5))

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
                     instance([(0, 0), (2, 2)], dim=2, source=(1, 1), library=((1, 1, 1),)),
                     instance([(5, 5), (1, 5), (9, 5)], dim=10, source=(1, 100), library=((1, 3, 1),)),
                     instance([(5, 5), (1, 5), (9, 5)], dim=10, source=(2, 1), library=((1, 3, 1),))]
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
