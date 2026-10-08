import pathlib,importlib.util,sys
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-51e094e/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-51e094e/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(2,1),(2,4),(3,4),(1,2),(1,3)];source=(2,4);library=((2,5,1),)
assert (here/'input001.cbi').read_text()==c.instance(pins,5,source,library,weight=100)
for name,expected in [('witness-output001.cbi',(6,3,2)),('witness-output002.cbi',(5,3,3))]:
 m=c.check_output((here/name).read_text(),pins,5,source,library);assert m==expected
 print(name,'legal',m,'Score',100*(m[0]-m[1])+m[2])
