import pathlib,sys,importlib.util
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-fef704e/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-fef704e/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(32,0)]+[(x,y) for x in range(1,64,2) for y in range(1,64,2)];source=(1,200);library=((4,200,1),)
assert (here/'input001.cbi').read_text()==c.instance(pins,64,source,library,weight=100)
for name,expected in [('actual-output001.cbi',(94,94,1023)),('witness-output001.cbi',(94,94,341))]:
 m=c.check_output((here/name).read_text(),pins,64,source,library);assert m==expected
 print(name,'legal',m,'Score',100*(m[0]-m[1])+m[2])
