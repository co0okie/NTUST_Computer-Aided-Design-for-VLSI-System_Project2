import pathlib,importlib.util,sys
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-7ab6560/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-7ab6560/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
pins=[(25,28),(11,10),(28,8),(23,15),(16,5),(10,28)];source=(3,34);library=((2,31,1),)
assert (here/'input001.cbi').read_text()==c.instance(pins,30,source,library,weight=12)
for name,expected in [('actual-output001.cbi',(55,48,4)),('witness-output001.cbi',(58,53,4))]:
 metrics=c.check_output((here/name).read_text(),pins,30,source,library);assert metrics==expected
 print(name,'legal',metrics,'Score',12*(metrics[0]-metrics[1])+metrics[2])
