import pathlib,importlib.util,sys
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
sys.argv=['checker',str(root/'tmp/tester/review-36f8658/build/cbi')]
spec=importlib.util.spec_from_file_location('checker',root/'tmp/tester/review-36f8658/tests/test_solver.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
source=(2,100);library=((1,100,1),)
for num,near,original,witness in [('001',(7,5),(4,2,0),(4,4,1)),('002',(6,5),(4,1,0),(4,3,1))]:
 pins=[(5,5),near,(9,5)];assert (here/f'input{num}.cbi').read_text()==c.instance(pins,10,source,library,weight=100)
 for name,expected in [(f'actual-output{num}.cbi',original),(f'witness-output{num}.cbi',witness)]:
  m=c.check_output((here/name).read_text(),pins,10,source,library);assert m==expected
  print(name,'legal',m,'Score',100*(m[0]-m[1])+m[2])
