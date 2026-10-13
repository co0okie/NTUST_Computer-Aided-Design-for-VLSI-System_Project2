#!/usr/bin/env python3
"""Public strategy options, deadline output, and rejection/output-preservation checks."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from benchmark import read_problem, verify

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CliTests(unittest.TestCase):
    def test_strategies_and_deadlines(self):
        problem = read_problem(ROOT / 'example/input.cbi')
        with tempfile.TemporaryDirectory() as folder:
            output, stats = pathlib.Path(folder)/'output.cbi', pathlib.Path(folder)/'stats.json'
            for strategy in ('current', 'sa_lp', 'hybrid'):
                run = subprocess.run([str(BINARY),str(ROOT/'example/input.cbi'),str(output),'--strategy',strategy,
                    '--time','0.1','--seed','17','--stats',str(stats)], capture_output=True,text=True,timeout=5)
                self.assertEqual(run.returncode,0,run.stderr)
                checked = verify(problem,output.read_text())
                data=json.loads(stats.read_text())
                self.assertEqual(checked['score'],data['score'])
                self.assertEqual(data['strategy'],strategy)
                self.assertLess(data['seconds'],1)

    def test_invalid_options_preserve_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder)/'output.cbi'
            for arguments in (['--time','60'], ['--time','nan'], ['--time','0'], ['--lp-time','0'],
                ['--locality','2'], ['--locality-k','0'], ['--cooling-ratio','0'], ['--temperature','-1'],
                ['--iterations','-1'], ['--strategy','unknown'], ['--move-weights','0,0,0,0,0,0,0'],
                ['--move-weights','1,2'], ['--unknown','3'], ['--seed']):
                output.write_text('preserved')
                run=subprocess.run([str(BINARY),str(ROOT/'example/input.cbi'),str(output)]+arguments,
                    capture_output=True,text=True,timeout=5)
                self.assertNotEqual(run.returncode,0,arguments)
                self.assertEqual(output.read_text(),'preserved')

    def test_fixed_iterations(self):
        with tempfile.TemporaryDirectory() as folder:
            output=pathlib.Path(folder)/'output.cbi'
            command=[str(BINARY),str(ROOT/'example/input.cbi'),str(output),'--strategy','sa_lp',
                '--seed','7','--iterations','60','--time','5']
            first=subprocess.run(command,capture_output=True,text=True,timeout=10)
            self.assertEqual(first.returncode,0,first.stderr)
            expected=output.read_text()
            second=subprocess.run(command,capture_output=True,text=True,timeout=10)
            self.assertEqual(second.returncode,0,second.stderr)
            self.assertEqual(output.read_text(),expected)
            self.assertEqual(second.stdout,first.stdout)


if __name__ == '__main__':
    unittest.main()
