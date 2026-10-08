import pathlib,importlib.util,sys
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-9233163/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-9233163/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(17,2),(16,5),(11,14),(11,17),(19,2),(1,6)];source=(3,45);library=((3,34,9),)
assert (here/'input001.cbi').read_text()==c.instance(pins,20,source,library,weight=1)
for name,expected in [('actual-output001.cbi',(26,20,18)),('witness-output001.cbi',(26,21,18))]:
 m=c.check_output((here/name).read_text(),pins,20,source,library);assert m==expected
 print(name,'legal',m,'Score',m[0]-m[1]+m[2])
