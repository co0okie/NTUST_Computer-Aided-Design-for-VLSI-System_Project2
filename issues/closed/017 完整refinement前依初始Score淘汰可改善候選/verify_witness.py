import pathlib,sys,importlib.util
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-1ed4520/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-1ed4520/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(9,9),(9,3),(17,24),(24,6)];source=(3,32);library=((2,11,4),(2,18,13))
assert (here/'input001.cbi').read_text()==c.instance(pins,30,source,library,weight=10)
for name,expected in [('actual-output001.cbi',(28,27,20)),('witness-output001.cbi',(23,22,17))]:
 m=c.check_output((here/name).read_text(),pins,30,source,library);assert m==expected
 print(name,'legal',m,'Score',10*(m[0]-m[1])+m[2])
