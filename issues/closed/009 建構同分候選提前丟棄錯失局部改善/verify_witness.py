import importlib.util
import pathlib
import sys
here=pathlib.Path(__file__).resolve().parent
root=here.parents[2]
sys.argv=["checks",str(root/"tmp/tester/candidate-review-build/cbi")]
spec=importlib.util.spec_from_file_location("checks",root/"tests/test_solver.py")
c=importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
pins=[(25,19),(14,6),(16,19),(18,8),(11,11),(20,13),(23,25),(18,12),(14,20)]
source=(1,37)
library=((2,15,1),)
weight=10
assert (here/"input001.cbi").read_text()==c.instance(pins,30,source,library,weight=weight)
for name in ["actual-output001.cbi","witness-output001.cbi"]:
 maximum,minimum,cost=c.check_output((here/name).read_text(),pins,30,source,library)
 print(f"{name}: legal; T_max={maximum}; T_min={minimum}; cost={cost}; Score={weight*(maximum-minimum)+cost}")
