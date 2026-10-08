import importlib.util
import pathlib
import sys
here=pathlib.Path(__file__).resolve().parent
root=here.parents[2]
sys.argv=["checks",str(root/"tmp/tester/local-multiway-build/cbi")]
spec=importlib.util.spec_from_file_location("checks",root/"tests/test_solver.py")
c=importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
pins=[(10,2),(12,10),(10,12),(8,10),(9,9)]
source=(1,8)
library=((3,100,10),)
assert (here/"input001.cbi").read_text()==c.instance(pins,20,source,library,weight=1)
for name in ["actual-output001.cbi","witness-output001.cbi"]:
 maximum,minimum,cost=c.check_output((here/name).read_text(),pins,20,source,library)
 print(f"{name}: legal; T_max={maximum}; T_min={minimum}; cost={cost}; Score={maximum-minimum+cost}")
