import pathlib,sys,importlib.util,subprocess
f=pathlib.Path(__file__).resolve().parent;root=f.parents[3];binary=pathlib.Path(sys.argv[1]).resolve();saved=sys.argv[:];sys.argv=['checker',str(binary)];s=importlib.util.spec_from_file_location('checker',root/'tests/test_solver.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c);sys.argv=saved
pins=[(12,28),(34,20),(24,13),(38,19),(20,1),(9,35),(37,29),(1,12),(15,26),(34,18),(26,35)]
def verify(path):
 hi,lo,cost=c.check_output(path.read_text(),pins,40,(3,78),((4,84,1),));return hi,lo,cost,cost+9*(hi-lo)
assert verify(f/'actual001.cbi')==(49,44,5,50)
assert verify(f/'witness001.cbi')==(49,46,7,34)
out=root/'tmp/tester/022-current.cbi';out.parent.mkdir(parents=True,exist_ok=True);res=subprocess.run([str(binary),str(f/'input001.cbi'),str(out)],capture_output=True,text=True,timeout=60);assert res.returncode==0,res.stderr
m=verify(out);assert res.stdout.strip()==f'T_max: {m[0]}, T_min: {m[1]}, Score: {m[3]}';print('actual/witness independently valid; current (max,min,cost,score):',m)
