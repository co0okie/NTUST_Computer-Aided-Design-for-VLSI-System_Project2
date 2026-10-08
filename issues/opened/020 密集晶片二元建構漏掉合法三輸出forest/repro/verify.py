import pathlib,sys,importlib.util,subprocess
f=pathlib.Path(__file__).resolve().parent;root=f.parents[3];binary=pathlib.Path(sys.argv[1]).resolve();saved=sys.argv[:];sys.argv=['checker',str(binary)];s=importlib.util.spec_from_file_location('checker',root/'tests/test_solver.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c);sys.argv=saved
cases=[([(2,2),(1,1),(1,3),(2,1),(3,1),(3,3),(2,3)],4,(2,2),((3,4,1),),(3,2,2)), ([(1,1),(3,1),(4,1),(3,2),(1,3),(1,4),(2,3),(3,3),(4,2),(2,4)],5,(3,4),((3,6,1),),(4,2,3))]
failed=0
for i,(pins,dim,source,lib,expected) in enumerate(cases,1):
 assert c.check_output((f/f'witness{i:03}.cbi').read_text(),pins,dim,source,lib)==expected
 out=root/f'tmp/tester/020-current{i:03}.cbi';out.parent.mkdir(parents=True,exist_ok=True);res=subprocess.run([str(binary),str(f/f'input{i:03}.cbi'),str(out)],capture_output=True,text=True,timeout=60)
 if res.returncode:failed+=1;print(i,'valid witness, solver rejected:',res.returncode,res.stderr.strip());continue
 hi,lo,cost=c.check_output(out.read_text(),pins,dim,source,lib);score=cost+100*(hi-lo);assert res.stdout.strip()==f'T_max: {hi}, T_min: {lo}, Score: {score}';print(i,'valid output:',(hi,lo,cost,score))
sys.exit(bool(failed))
