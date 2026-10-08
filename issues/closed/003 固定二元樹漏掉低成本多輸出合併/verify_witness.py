import importlib.util
import pathlib
import sys
here = pathlib.Path(__file__).resolve().parent
root = here.parents[2]
saved = sys.argv[:]
sys.argv = [str(root / "tests/test_solver.py"), "/tmp/cbi-tester-build/cbi"]
spec = importlib.util.spec_from_file_location("independent_checks", root / "tests/test_solver.py")
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)
sys.argv = saved
pins = [(5, 1), (4, 5), (5, 6), (6, 5), (5, 4)]
source = (1, 4)
library = ((4, 20, 10),)
assert (here / "input001.cbi").read_text() == checks.instance(pins, 10, source, library)
maximum, minimum, cost = checks.check_output((here / "witness-output001.cbi").read_text(), pins, 10, source, library)
print(f"Witness valid: T_max={maximum}, T_min={minimum}, cost={cost}, Score={100 * (maximum-minimum) + cost}")
