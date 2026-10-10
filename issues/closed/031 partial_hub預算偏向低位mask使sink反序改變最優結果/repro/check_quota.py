"""Reproduce the diagnostic budget-only change in ignored tester storage."""
import pathlib,subprocess,sys
folder=pathlib.Path(__file__).resolve().parent
root=folder.parents[3]
work=root/'tmp/tester/031/quota-probe';work.mkdir(parents=True,exist_ok=True)
for name in ('main.cpp','model.hpp','tree.hpp'):
    source=subprocess.check_output(['git','-C',str(root),'show','7d87ec4:src/'+name],text=True)
    if name=='tree.hpp':
        start=source.index('Tree partial_hub_tree');end=source.index('Tree joint_bypass_tree',start)
        source=source[:start]+source[start:end].replace('8192','262144')+source[end:]
    (work/name).write_text(source)
subprocess.run(['c++','-std=c++17','-O2',str(work/'main.cpp'),'-o',str(work/'cbi')],check=True)
subprocess.run([sys.executable,str(folder/'verify.py'),str(work/'cbi')],check=True)
