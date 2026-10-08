import importlib.util
import pathlib
import sys
here=pathlib.Path(__file__).resolve().parent
root=here.parents[2]
sys.argv=["checks",str(root/"tmp/tester/relay-cost-build/cbi")]
spec=importlib.util.spec_from_file_location("checks",root/"tests/test_solver.py")
c=importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
pins=[(2,2),(32,2)]
source=(1,1)
library=((1,5,1),(1,100,100))
assert (here/"input001.cbi").read_text()==c.instance(pins,40,source,library,weight=1)
for name in ["actual-output001.cbi","witness-output001.cbi"]:
 maximum,minimum,cost=c.check_output((here/name).read_text(),pins,40,source,library)
 print(f"{name}: legal; T_max={maximum}; T_min={minimum}; cost={cost}; Score={maximum-minimum+cost}")
