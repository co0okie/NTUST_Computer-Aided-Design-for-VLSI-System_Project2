#!/usr/bin/env python3
"""Small parameter sweeps on caller-selected training or holdout cases."""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import platform
import shutil
import benchmark


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--inputs',type=pathlib.Path,nargs='+',required=True)
    parser.add_argument('--seeds',type=int,nargs='+',default=[1,2,3])
    parser.add_argument('--budget',type=float,default=1)
    parser.add_argument('--jobs',type=int,default=1)
    parser.add_argument('--holdout',action='store_true')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    binary=args.output/'cbi';shutil.copy2(args.binary.resolve(),binary)
    configurations={'default':[], 'cost_moves':['--move-weights','20,15,10,25,15,10,5']}
    if not args.holdout:
        configurations.update({'global':['--locality','0'],'local_only':['--locality','1'],
            'repair64':['--repair-trials','64'],'no_wire_tiebreak':['--wire-tiebreak','0']})
    tasks=[]
    for configuration,extra in configurations.items():
        for path in args.inputs:
            for seed in args.seeds:
                tasks.append((configuration,(binary.resolve(),path,'hybrid',seed,args.budget,
                    args.output/f'run{len(tasks):05}',extra)))
    rows=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures={executor.submit(benchmark.run_case,*task):configuration for configuration,task in tasks}
        for future in concurrent.futures.as_completed(futures):
            row=future.result();row['configuration']=futures[future];rows.append(row)
            (args.output/'results.json').write_text(json.dumps(rows,indent=2))
    metadata={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'platform':platform.platform(),'configurations':configurations,'seeds':args.seeds,
        'jobs':args.jobs,'budget':args.budget,'inputs':list(map(str,args.inputs))}
    (args.output/'metadata.json').write_text(json.dumps(metadata,indent=2))
    print(f'{len(rows)} runs complete; {sum(not row["legal"] for row in rows)} failures')


if __name__=='__main__':main()
