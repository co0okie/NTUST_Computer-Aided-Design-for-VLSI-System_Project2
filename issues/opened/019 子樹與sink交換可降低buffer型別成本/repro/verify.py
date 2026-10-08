import pathlib,sys,importlib.util,subprocess,re
f=pathlib.Path(__file__).resolve().parent
root=f.parents[3]
binary=pathlib.Path(sys.argv[1]).resolve()
saved=sys.argv[:];sys.argv=['checker',str(binary)]
s=importlib.util.spec_from_file_location('checker',root/'tests/test_solver.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c);sys.argv=saved
pins=[(4,17),(3,1),(21,10),(29,18),(16,19),(14,20),(20,29),(13,12),(29,15)]
def verify(path):
 text=path.read_text();hi,lo,cost=c.check_output(text,pins,30,(4,16),((2,17,7),(2,66,13)))
 return hi,lo,cost,cost+15*(hi-lo)
assert verify(f/'actual001.cbi')==(32,29,67,112)
assert verify(f/'witness001.cbi')==(32,29,61,106)
out=root/'tmp/tester/019-current.cbi';out.parent.mkdir(parents=True,exist_ok=True)
res=subprocess.run([str(binary),str(f/'input001.cbi'),str(out)],capture_output=True,text=True,timeout=60);assert res.returncode==0,res.stderr
m=verify(out);assert res.stdout.strip()==f'T_max: {m[0]}, T_min: {m[1]}, Score: {m[3]}'
print('actual/witness independently valid; current (max,min,cost,score):',m)
