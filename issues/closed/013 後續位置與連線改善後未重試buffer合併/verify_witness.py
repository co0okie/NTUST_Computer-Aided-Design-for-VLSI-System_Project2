import pathlib,importlib.util,sys
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-83e4791/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-83e4791/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(13,14),(3,13),(2,2),(15,17),(10,19),(19,5),(6,28)];source=(3,25);library=((3,39,7),)
assert (here/'input001.cbi').read_text()==c.instance(pins,30,source,library,weight=3)
for name,expected in [('actual-output001.cbi',(24,23,21)),('witness-output001.cbi',(24,23,14))]:
 m=c.check_output((here/name).read_text(),pins,30,source,library);assert m==expected
 print(name,'legal',m,'Score',3*(m[0]-m[1])+m[2])
