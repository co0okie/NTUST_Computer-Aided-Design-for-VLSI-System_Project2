#!/usr/bin/env python3
"""Summarize paired ablations without dropping failures from the verdict."""
import argparse,csv,json,pathlib,statistics
from collections import defaultdict

def summarize(rows,phase):
    data=[r for r in rows if r['phase']==phase]
    grouped=defaultdict(list)
    for row in data:grouped[row['variant']].append(row)
    baseline={(r['input'],r['seed'],r['budget']):r for r in grouped['baseline']}
    result=[]
    for variant,group in sorted(grouped.items()):
        paircounts=defaultdict(int);casegroups=defaultdict(list);ratios=[];durations=[]
        for r in group:
            b=baseline.get((r['input'],r['seed'],r['budget']))
            if b is None:continue
            if not b['legal']:
                paircounts['baseline_failed']+=1
                continue
            if not r['legal']:
                paircounts['failed']+=1
                continue
            direction='better' if r['score']<b['score'] else 'worse' if r['score']>b['score'] else 'equal'
            paircounts[direction]+=1
            casegroups[(r['input'],r['budget'])].append((b['score'],r['score']))
            if b['score']>0:ratios.append((r['score']-b['score'])/b['score'])
            if b['seconds']>0:durations.append(r['seconds']/b['seconds'])
        cases=defaultdict(int);fullseedcount=1 if phase in ['legacy','legacy_holdout','init_stress'] else 3
        for case,pairs in casegroups.items():
            if len(pairs)!=fullseedcount:
                cases['incomplete']+=1
                continue
            before=statistics.median(a for a,b in pairs);after=statistics.median(b for a,b in pairs)
            cases['better' if after<before else 'worse' if after>before else 'equal']+=1
        attempts=[sum(m['attempts'] for m in r.get('statistics',{}).get('moves',[])) for r in group]
        row={'phase':phase,'variant':variant,'runs':len(group),'legal':sum(r['legal'] for r in group),
             'paired_better':paircounts['better'],'paired_equal':paircounts['equal'],'paired_worse':paircounts['worse'],
             'paired_failed':paircounts['failed'],'baseline_failed':paircounts['baseline_failed'],
             'case_better':cases['better'],'case_equal':cases['equal'],'case_worse':cases['worse'],
             'incomplete_cases':cases['incomplete'],
             'mean_relative_score_delta':statistics.mean(ratios) if ratios else None,
             'median_time_ratio':statistics.median(durations) if durations else None,
             'total_seconds':sum(r['seconds'] for r in group),
             'lp_calls':sum(r.get('statistics',{}).get('lp_calls',0) for r in group),
             'lp_feasible':sum(r.get('statistics',{}).get('lp_feasible',0) for r in group),
             'repair_calls':sum(r.get('statistics',{}).get('repair_calls',0) for r in group),
             'iteration_censored':sum(r.get('statistics',{}).get('termination')==1 and a<r['iterations'] for r,a in zip(group,attempts) if r['iterations']),
             'attempts':sum(attempts)}
        result.append(row)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path)
    a=p.parse_args();rows=[json.loads(p.read_text()) for p in a.folder.glob('*.json') if len(p.stem)==16]
    phases=sorted({r['phase'] for r in rows}-{'verify','verify_sa'})
    result=[r for phase in phases for r in summarize(rows,phase)]
    out=a.output or a.folder
    out.mkdir(exist_ok=True,parents=True)
    (out/'summary.json').write_text(json.dumps(result,indent=2))
    if result:
        with (out/'summary.csv').open('w',newline='') as f:
            w=csv.DictWriter(f,list(result[0]));w.writeheader();w.writerows(result)
    for r in result:
        print(r['phase'],r['variant'],f"legal {r['legal']}/{r['runs']}",f"case +/-/= {r['case_better']}/{r['case_worse']}/{r['case_equal']}",f"failed {r['paired_failed']}",f"time {r['median_time_ratio']}",flush=True)
if __name__=='__main__':main()
