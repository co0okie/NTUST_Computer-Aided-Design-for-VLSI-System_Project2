import importlib.util,pathlib,sys
here=pathlib.Path(__file__).resolve().parent
root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-bf97d1d/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-bf97d1d/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(26,8),(12,25),(29,2),(15,23),(26,14)];source=(1,19);library=((2,22,10),)
assert (here/'input001.cbi').read_text()==c.instance(pins,30,source,library,weight=1)
for name,expected in [('actual-output001.cbi',(36,34,30)),('witness-output001.cbi',(36,35,30))]:
 metrics=c.check_output((here/name).read_text(),pins,30,source,library);assert metrics==expected
 print(name,'legal',metrics,'Score',metrics[0]-metrics[1]+metrics[2])
