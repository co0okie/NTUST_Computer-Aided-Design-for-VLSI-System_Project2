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
            pins = [(5, 5)] + sinks
            skew, text = self.run_case(pins, dim=10, source=(2, 3), library=((1, 3, 1),))
            _, _, cost = check_output(text, pins, 10, (2, 3), ((1, 3, 1),))
            # Delay insertion may spend a second buffer to reduce weighted skew.
            self.assertLessEqual(100 * skew + cost, 301)

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

    def test_branch_type_and_relay_cost(self):
        pins = [(3, 3), (20, 10), (20, 30)]
        for library, bound in [(((2, 6, 1), (2, 100, 100)), 5),
                                (((2, 6, 10), (2, 100, 4)), 4),
                                (((1, 6, 1), (2, 100, 100)), 100)]:
            skew, text = self.run_case(pins, dim=40, source=(1, 100), library=library, weight=1)
            _, _, cost = check_output(text, pins, 40, (1, 100), library)
            self.assertLessEqual(skew + cost, bound)

    def test_branch_budget_with_occupied_relay(self):
        # The extra fixed sink occupies an ideal relay of the two-sink tree.
        pins = [(3, 3), (20, 10), (20, 30), (20, 17)]
        library = ((3, 6, 1), (3, 100, 100))
        skew, text = self.run_case(pins, dim=40, source=(1, 100), library=library, weight=1)
        _, _, cost = check_output(text, pins, 40, (1, 100), library)
        self.assertLessEqual(skew + cost, 12)

    def test_local_multiway_merge(self):
        pins = [(10, 2), (12, 10), (10, 12), (8, 10), (9, 9)]
        skew, text = self.run_case(pins, dim=20, source=(1, 8), library=((3, 100, 10),), weight=1)
        _, _, cost = check_output(text, pins, 20, (1, 8), ((3, 100, 10),))
        self.assertEqual((skew, cost), (0, 20))
        self.assertTrue(text.startswith('.buffer 2\n'))

    def test_delay_relay_weighted_skew_and_parity(self):
        library = ((1, 100, 1),)
        for near, bound in [((7, 5), 1), ((6, 5), 101)]:
            for order in [[near, (9, 5)], [(9, 5), near]]:
                pins = [(5, 5)] + order
                skew, text = self.run_case(pins, dim=10, source=(2, 100), library=library, weight=100)
                _, _, cost = check_output(text, pins, 10, (2, 100), library)
                self.assertEqual(100 * skew + cost, bound)

    def test_delay_relay_cost_limits_and_boundary(self):
        pins = [(5, 5), (7, 5), (9, 5)]
        skew, text = self.run_case(pins, dim=10, source=(2, 100), library=((1, 100, 10),), weight=1)
        self.assertEqual(skew, 2)
        self.assertTrue(text.startswith('.buffer 0\n'))
        # A relay of capacity one must sit next to the near sink; delaying it
        # then requires SRC outgoing sum >=7, exceeding this limit of six.
        skew, text = self.run_case(pins, dim=10, source=(2, 6), library=((1, 1, 1),), weight=100)
        self.assertEqual(skew, 2)
        self.assertTrue(text.startswith('.buffer 0\n'))
        pins = [(5, 0), (7, 0), (9, 0)]
        skew, text = self.run_case(pins, dim=10, source=(2, 100), library=((1, 100, 1),), weight=100)
        _, _, cost = check_output(text, pins, 10, (2, 100), ((1, 100, 1),))
        self.assertEqual((skew, cost), (0, 1))

    def test_delay_relay_huge_dimensions_and_int64(self):
        cases = [([(5, 5), (7, 5), (10**12 - 5, 5)], 10**12,
                  (2, 2 * 10**12), ((1, 10**12, 1),), 100, 1),
                 ([(0, 0), (1, 1), (2**63 - 5, 0)], 2**63 - 1,
                  (2, 2**63 - 1), ((1, 2**63 - 1, 1),), 1, 3)]
        for pins, dim, source, library, weight, bound in cases:
            skew, text = self.run_case(pins, dim=dim, source=source, library=library, weight=weight)
            _, _, cost = check_output(text, pins, dim, source, library)
            self.assertLessEqual(weight * skew + cost, bound)

    def test_sink_reparent_uses_remaining_capacity(self):
        sinks = [(16, 5), (11, 14), (11, 17), (19, 2), (1, 6)]
        library = ((3, 34, 9),)
        for order in [sinks, list(reversed(sinks))]:
            pins = [(17, 2)] + order
            skew, text = self.run_case(pins, dim=20, source=(3, 45), library=library, weight=1)
            _, _, cost = check_output(text, pins, 20, (3, 45), library)
            self.assertLessEqual(skew + cost, 23)

    def test_revisit_merge_after_later_improvements(self):
        sinks = [(3, 13), (2, 2), (15, 17), (10, 19), (19, 5), (6, 28)]
        library = ((3, 39, 7),)
        for order in [sinks, list(reversed(sinks))]:
            pins = [(13, 14)] + order
            skew, text = self.run_case(pins, dim=30, source=(3, 25), library=library, weight=3)
            _, _, cost = check_output(text, pins, 30, (3, 25), library)
            self.assertEqual((skew, cost), (1, 14))
            self.assertTrue(text.startswith('.buffer 2\n'))

    def test_refinement_large_tree_fallback(self):
        pins = [(0, 0)] + [(10 * x, 10 * y) for x in range(1, 27) for y in range(1, 11)][:257]
        _, text = self.run_case(pins, dim=10**12, source=(1, 10**12),
                                library=((2, 10**12, 1),), weight=1)
        self.assertGreaterEqual(int(text.splitlines()[0].split()[1]), 256)

    def test_large_tree_multioutput_contraction(self):
        sinks = [(x, y) for x in range(1, 64, 2) for y in range(1, 64, 2)]
        for order in [sinks, list(reversed(sinks))]:
            pins = [(32, 0)] + order
            skew, text = self.run_case(pins, dim=64, source=(1, 200), library=((4, 200, 1),))
            _, _, cost = check_output(text, pins, 64, (1, 200), ((4, 200, 1),))
            self.assertEqual((skew, cost), (0, 341))

    def test_cross_branch_sink_exchange(self):
        sinks = [(11, 10), (28, 8), (23, 15), (16, 5), (10, 28)]
        library = ((2, 31, 1),)
        for order in [sinks, list(reversed(sinks))]:
            pins = [(25, 28)] + order
            skew, text = self.run_case(pins, dim=30, source=(3, 34), library=library, weight=12)
            _, _, cost = check_output(text, pins, 30, (3, 34), library)
            self.assertLessEqual(12 * skew + cost, 64)

    def test_subtree_and_sink_exchange(self):
        sinks = [(3, 1), (21, 10), (29, 18), (16, 19), (14, 20),
                 (20, 29), (13, 12), (29, 15)]
        library = ((2, 17, 7), (2, 66, 13))
        for order in [sinks, list(reversed(sinks)), sinks[3:] + sinks[:3]]:
            pins = [(4, 17)] + order
            skew, text = self.run_case(pins, dim=30, source=(4, 16), library=library, weight=15)
            _, _, cost = check_output(text, pins, 30, (4, 16), library)
            self.assertLessEqual(15 * skew + cost, 106)

    def test_final_delay_revisit(self):
        sinks = [(34, 20), (24, 13), (38, 19), (20, 1), (9, 35),
                 (37, 29), (1, 12), (15, 26), (34, 18), (26, 35)]
        library = ((4, 84, 1),)
        for order in [sinks, list(reversed(sinks)), sinks[3:] + sinks[:3]]:
            pins = [(12, 28)] + order
            skew, text = self.run_case(pins, dim=40, source=(3, 78), library=library, weight=9)
            _, _, cost = check_output(text, pins, 40, (3, 78), library)
            self.assertLessEqual(9 * skew + cost, 34)

    def test_sink_exchange_rejects_parent_length_overflow(self):
        # Swapping the direct near sink with the deeper far sink would lower
        # skew from 11 to 7, but SRC's outgoing total would become 12 > 3.
        pins = [(0, 0), (1, 0), (10, 0)]
        library = ((1, 20, 1),)
        skew, text = self.run_case(pins, dim=20, source=(2, 3), library=library, weight=1)
        _, _, cost = check_output(text, pins, 20, (2, 3), library)
        self.assertEqual((skew, cost), (11, 1))

    def test_sink_exchange_large_input_bound(self):
        pins = [(0, 0)] + [(10 * x, 10 * y) for x in range(1, 14) for y in range(1, 11)][:129]
        _, text = self.run_case(pins, dim=10**12, source=(1, 10**12),
                                library=((2, 10**12, 1),), weight=1)
        self.assertGreaterEqual(int(text.splitlines()[0].split()[1]), 128)

    def test_buffer_position_improvement(self):
        sinks = [(12, 25), (29, 2), (15, 23), (26, 14)]
        library = ((2, 22, 10),)
        for order in [sinks, list(reversed(sinks))]:
            pins = [(26, 8)] + order
            skew, text = self.run_case(pins, dim=30, source=(1, 19), library=library, weight=1)
            _, _, cost = check_output(text, pins, 30, (1, 19), library)
            self.assertEqual((skew, cost), (1, 30))

    def test_buffer_position_bounds_and_occupied_target(self):
        base = [(26, 8), (12, 25), (29, 2), (15, 23), (26, 14)]
        library = ((2, 22, 10),)
        cases = [(base + [(12, 15)], 30, 41),
                 ([(x - 11, y - 2) for x, y in base], 30, 31),
                 (base, 10**12, 31)]
        for pins, dim, bound in cases:
            skew, text = self.run_case(pins, dim=dim, source=(1, 19), library=library, weight=1)
            _, _, cost = check_output(text, pins, dim, (1, 19), library)
            self.assertLessEqual(skew + cost, bound)

    def test_branch_budget_allocation(self):
        library = ((2, 6, 5), (1, 6, 1))
        for sinks in [[(20, 10), (20, 30)], [(20, 30), (20, 10)]]:
            pins = [(3, 3)] + sinks
            skew, text = self.run_case(pins, dim=40, source=(1, 100), library=library, weight=1)
            _, _, cost = check_output(text, pins, 40, (1, 100), library)
            self.assertEqual((skew, cost), (0, 8))

    def test_branch_budget_short_distance_and_occupied_launch(self):
        pins = [(20, 18), (20, 10), (20, 30)]
        library = ((2, 6, 5), (1, 6, 1))
        skew, text = self.run_case(pins, dim=40, source=(1, 100), library=library, weight=1)
        _, _, cost = check_output(text, pins, 40, (1, 100), library)
        self.assertEqual((skew, cost), (0, 8))
        pins = [(3, 3), (20, 19), (20, 30)]
        library = ((2, 12, 5), (1, 6, 1))
        skew, text = self.run_case(pins, dim=40, source=(1, 100), library=library, weight=1)
        _, _, cost = check_output(text, pins, 40, (1, 100), library)
        self.assertEqual((skew, cost), (1, 5))

    def test_branch_budget_huge_length(self):
        pins = [(3, 3), (20, 10), (20, 10**12 + 30)]
        library = ((2, 5 * 10**11, 5), (1, 6, 1))
        dim = 10**12 + 40
        skew, text = self.run_case(pins, dim=dim, source=(1, 10**12), library=library, weight=1)
        _, _, cost = check_output(text, pins, dim, (1, 10**12), library)
        self.assertLessEqual(skew + cost, 20)

    def test_equal_score_candidate_improvement(self):
        pins = [(25, 19), (14, 6), (16, 19), (18, 8), (11, 11),
                (20, 13), (23, 25), (18, 12), (14, 20)]
        library = ((2, 15, 1),)
        skew, text = self.run_case(pins, dim=30, source=(1, 37), library=library, weight=10)
        _, _, cost = check_output(text, pins, 30, (1, 37), library)
        self.assertLessEqual(10 * skew + cost, 17)

    def test_local_merge_fanout_and_total_length(self):
        pins = [(10, 2), (12, 10), (10, 12), (8, 10), (9, 9)]
        _, text = self.run_case(pins, dim=20, source=(1, 8), library=((2, 100, 10),), weight=1)
        self.assertGreaterEqual(int(text.splitlines()[0].split()[1]), 3)
        # Edges of the two-buffer witness are individually <=3, but its
        # outgoing total exceeds 3. All leaves span x+y ranges totaling 7,
        # which two buffers' combined outgoing budgets of 6 cannot cover.
        _, text = self.run_case(pins, dim=20, source=(1, 8), library=((3, 3, 10),), weight=1)
        self.assertGreaterEqual(int(text.splitlines()[0].split()[1]), 3)

    def test_merge_uses_weighted_skew(self):
        pins = [(3, 3), (20, 10), (20, 30)]
        library = ((2, 100, 30),)
        for weight, expected in [(1, (20, 0)), (100, (0, 30))]:
            skew, text = self.run_case(pins, dim=40, source=(2, 100), library=library, weight=weight)
            _, _, cost = check_output(text, pins, 40, (2, 100), library)
            self.assertEqual((skew, cost), expected)

    def test_same_boundary_relay(self):
        # Each edge's shortest path lies on a forbidden buffer boundary.
        for pins in [[(10, 0), (90, 0)], [(10, 100), (90, 100)],
                     [(0, 10), (0, 90)], [(100, 10), (100, 90)],
                     [(0, 0), (100, 0)]]:
            skew, _ = self.run_case(pins, source=(1, 5), library=((1, 10, 1),))
            self.assertEqual(skew, 0)

    def test_direct_multiway_grid(self):
        # Distinct integer edges force at least ceil((N - SRC fanout) / 3)
        # buffers. These four-output grids attain that bound without skew.
        for side in (8, 16, 32):
            sinks = [(x, y) for x in range(1, side, 2) for y in range(1, side, 2)]
            for ordered in (sinks, list(reversed(sinks))):
                pins = [(side // 2, 0)] + ordered
                skew, text = self.run_case(pins, dim=side, source=(1, 1000),
                                          library=((4, 1000, 1),))
                self.assertEqual(skew, 0)
                self.assertEqual(int(text.splitlines()[0].split()[1]), (len(sinks) - 1) // 3)

    def test_multiway_and_adaptive_regressions(self):
        # Fixed-seed discoveries: a formerly missed legal tree, and two
        # lower-cost results from the extra construction/refinement search.
        cases = [
            ([(6, 8), (7, 6), (3, 8), (2, 2), (0, 3), (7, 4), (1, 1),
              (5, 7), (2, 1), (8, 7), (6, 0), (8, 8), (3, 7), (3, 0),
              (2, 5), (8, 1), (4, 2), (4, 8)],
             8, (2, 2), ((5, 100, 5), (1, 3, 9)), 1, 33),
            ([(11, 4), (5, 7), (1, 6), (6, 4), (5, 11), (3, 0), (6, 2),
              (7, 2), (3, 1), (11, 8), (11, 9)],
             12, (2, 5), ((1, 40, 7), (3, 20, 9), (7, 10, 11)), 20, 101),
            ([(6, 7), (4, 3), (2, 6), (3, 5), (8, 5), (7, 0), (7, 7),
              (6, 0), (6, 3), (8, 8), (3, 6), (4, 4), (3, 7)],
             8, (4, 20), ((2, 40, 4), (4, 100, 5), (4, 5, 2)), 20, 59),
        ]
        for pins, dim, source, library, weight, ceiling in cases:
            with self.subTest(pins=pins):
                skew, text = self.run_case(pins, dim, source, library, weight)
                _, _, cost = check_output(text, pins, dim, source, library)
                self.assertLessEqual(weight * skew + cost, ceiling)

    def test_dense_forest_with_few_free_positions(self):
        cases = [
            [(2, 4), (3, 4), (0, 0), (4, 0), (4, 1), (0, 3), (2, 2),
             (3, 3), (2, 0), (1, 0), (4, 4), (1, 4), (1, 2), (3, 0),
             (4, 2), (2, 3)],
            [(0, 4), (0, 2), (2, 4), (3, 4), (2, 1), (0, 0), (4, 0),
             (4, 1), (0, 3), (0, 1), (3, 1), (2, 2), (3, 3), (2, 0),
             (1, 0), (4, 4), (1, 4), (1, 2), (3, 0), (4, 2), (2, 3)],
        ]
        for sinks in cases:
            for ordered in (sinks, list(reversed(sinks))):
                with self.subTest(sinks=ordered):
                    _, text = self.run_case([(4, 3)] + ordered, dim=4, source=(3, 10),
                                            library=((9, 16, 1),), weight=1)
                    self.assertEqual(int(text.splitlines()[0].split()[1]), 3)

    def test_hub_source_axis_crossings(self):
        for library in (((2, 17, 8), (2, 41, 12)), ((2, 41, 12), (2, 17, 8))):
            for sinks in ([(6, 13), (3, 13)], [(3, 13), (6, 13)]):
                for transpose in (False, True):
                    pins = [(0, 2)] + sinks
                    if transpose:
                        pins = [(y, x) for x, y in pins]
                    skew, text = self.run_case(pins, dim=14, source=(1, 5),
                                              library=library, weight=26)
                    _, _, cost = check_output(text, pins, 14, (1, 5), library)
                    self.assertEqual(26 * skew + cost, 38)
                    self.assertEqual(int(text.splitlines()[0].split()[1]), 1)

    def test_joint_membership_and_position(self):
        for sinks in ([(2, 2), (13, 11), (6, 7)], [(6, 7), (13, 11), (2, 2)]):
            for transpose in (False, True):
                pins = [(2, 12)] + sinks
                if transpose:
                    pins = [(y, x) for x, y in pins]
                skew, text = self.run_case(pins, dim=15, source=(3, 23),
                                          library=((3, 16, 6),), weight=3)
                _, _, cost = check_output(text, pins, 15, (3, 23), ((3, 16, 6),))
                self.assertEqual(3 * skew + cost, 9)

    def test_random_sparse(self):
        rng = random.Random(20261009)
        for _ in range(40):
            pins = rng.sample([(x, y) for x in range(5, 96, 5) for y in range(5, 96, 5)], 21)
            self.run_case(pins, source=(2, 30), library=((2, 30, 2), (6, 80, 5)))

    def test_small_forest_construction_fallback(self):
        # Greedy local merge points block all original complete builders.
        sinks = [(2, 4), (3, 4), (1, 2), (1, 3)]
        for order in [sinks, list(reversed(sinks)), sinks[2:] + sinks[:2]]:
            skew, text = self.run_case([(2, 1)] + order, dim=5,
                                       source=(2, 4), library=((2, 5, 1),))
            _, _, cost = check_output(text, [(2, 1)] + order, 5, (2, 4), ((2, 5, 1),))
            self.assertLessEqual(100 * skew + cost, 302)

    def test_dense_multiway_forest_fallback(self):
        cases = [([(1, 1), (1, 3), (2, 1), (3, 1), (3, 3), (2, 3)],
                  (2, 2), 4, (2, 2), ((3, 4, 1),), 102),
                 ([(3, 1), (4, 1), (3, 2), (1, 3), (1, 4), (2, 3), (3, 3), (4, 2), (2, 4)],
                  (1, 1), 5, (3, 4), ((3, 6, 1),), 203)]
        for sinks, src, dim, limits, library, bound in cases:
            for order in [sinks, list(reversed(sinks)), sinks[2:] + sinks[:2]]:
                pins = [src] + order
                skew, text = self.run_case(pins, dim=dim, source=limits, library=library)
                _, _, cost = check_output(text, pins, dim, limits, library)
                self.assertLessEqual(100 * skew + cost, bound)

    def test_library_arity_dense_forest_fallback(self):
        cases = [([(1, 1), (1, 3), (2, 1), (0, 2), (3, 1), (3, 3), (2, 3), (4, 2)],
                  4, (2, 2), ((4, 5, 1),), 102),
                 ([(1, 1), (1, 3), (2, 1), (0, 1), (0, 3), (3, 1), (3, 3), (2, 3), (4, 1), (4, 3)],
                  4, (2, 2), ((5, 8, 1),), 102),
                 ([(1, 1), (1, 3), (1, 4), (2, 1), (3, 1), (4, 1), (4, 2), (4, 3),
                   (2, 4), (3, 3), (3, 4), (4, 4)], 5, (3, 3), ((4, 7, 1),), 203)]
        for sinks, dim, limits, library, bound in cases:
            for order in [sinks, list(reversed(sinks)), sinks[2:] + sinks[:2]]:
                pins = [(2, 2)] + order
                skew, text = self.run_case(pins, dim=dim, source=limits, library=library)
                _, _, cost = check_output(text, pins, dim, limits, library)
                self.assertLessEqual(100 * skew + cost, bound)

    def test_full_refinement_candidate_diversity(self):
        sinks = [(9, 3), (17, 24), (24, 6)]
        variants = [(((2, 11, 4), (2, 18, 13)), [(1, 21), (10, 27), (100, 117)]),
                    (((2, 11, 8), (2, 18, 26)), [(1, 33), (10, 44), (100, 134)]),
                    (((2, 11, 4), (2, 18, 5)), [(1, 10), (10, 19), (100, 109)])]
        for order in [sinks, list(reversed(sinks)), sinks[1:] + sinks[:1]]:
            for library, cases in variants:
                for weight, bound in cases:
                    pins = [(9, 9)] + order
                    skew, text = self.run_case(pins, dim=30, source=(3, 32), library=library, weight=weight)
                    _, _, cost = check_output(text, pins, 30, (3, 32), library)
                    self.assertLessEqual(weight * skew + cost, bound)

    def test_reject_without_overwriting(self):
        good = instance([(10, 10), (20, 20)])
        bad_cases = [good.replace('20 20', '10 10'), good.replace('20 20', '101 20'),
                     good.replace('fanout 2', 'fanout -2'), good.replace('w_skew 100', 'w_skew nope'),
                     good + 'unexpected\n', good.replace('.pin 2', '.pin 3'),
                     instance([(0, 0), (2, 2)], dim=2, source=(1, 1), library=((1, 1, 1),)),
                     instance([(5, 5), (1, 5), (9, 5)], dim=10, source=(1, 100), library=((1, 3, 1),)),
                     instance([(2, 1), (2, 4), (3, 4), (1, 2), (1, 3)], dim=5,
                              source=(2, 4), library=((2, 1, 1),)),
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
