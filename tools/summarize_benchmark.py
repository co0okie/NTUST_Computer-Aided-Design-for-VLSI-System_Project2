#!/usr/bin/env python3
"""Export compact, reviewable metrics and incumbent curves from benchmark JSON."""
import argparse
import collections
import csv
import json
import pathlib
import statistics


def normalized(path):
    if '/inputs/input' in path and path.startswith('tmp/coder/'):
        return 'benchmarks/sa_lp/inputs/' + pathlib.Path(path).name
    return path


def write_csv(path, rows, fields):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--short',type=pathlib.Path,required=True)
    parser.add_argument('--long',type=pathlib.Path,required=True)
    parser.add_argument('--ablation',type=pathlib.Path,required=True)
    parser.add_argument('--holdout',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    datasets={name:json.loads((getattr(args,name)/'results.json').read_text()) for name in ('short','long','ablation','holdout')}
    rows=[]
    for dataset,entries in datasets.items():
        for entry in entries:
            row=dict(entry); row['dataset']=dataset; row['input']=normalized(row['input']); rows.append(row)
    fields=['dataset','input','strategy','configuration','seed','budget','legal','score','skew','cost','buffers','seconds','rss_kib','error']
    write_csv(args.output/'runs.csv',rows,fields)
    curves=[]
    for row in rows:
        if row['dataset']!='long' or not row['legal']:continue
        for limit in (1,5,15,30,55):
            points=[p for p in row['statistics']['checkpoints'] if p['seconds']<=limit]
            point=min(points,key=lambda p:p['score']) if points else None
            curves.append({'input':row['input'],'strategy':row['strategy'],'seed':row['seed'],'seconds':limit,
                'score':point['score'] if point else '', 'skew':point['skew'] if point else ''})
    write_csv(args.output/'curves.csv',curves,['input','strategy','seed','seconds','score','skew'])
    moves=collections.defaultdict(lambda:collections.Counter())
    for row in rows:
        if row['dataset']!='short' or not row['legal']:continue
        for move in row['statistics']['moves']:
            totals=moves[row['strategy'],move['name']]
            for key in ('attempts','feasible','accepted','improvements','seconds'):totals[key]+=move[key]
    move_rows=[]
    for (strategy,name),counts in sorted(moves.items()):
        attempts=counts['attempts']
        move_rows.append({'strategy':strategy,'move':name,**counts,
            'feasible_rate':counts['feasible']/attempts if attempts else 0,
            'acceptance_rate':counts['accepted']/counts['feasible'] if counts['feasible'] else 0,
            'accepted_per_attempt':counts['accepted']/attempts if attempts else 0,
            'improvement_rate':counts['improvements']/attempts if attempts else 0,
            'mean_seconds':counts['seconds']/attempts if attempts else 0})
    write_csv(args.output/'moves.csv',move_rows,['strategy','move','attempts','feasible','accepted','improvements','seconds','feasible_rate','acceptance_rate','accepted_per_attempt','improvement_rate','mean_seconds'])
    grouped=collections.defaultdict(lambda:collections.defaultdict(list))
    for row in datasets['short']:grouped[row['input']][row['strategy']].append(row)
    summary={'short_cases':len(grouped),'runs':{k:len(v) for k,v in datasets.items()},'illegal_or_failed':sum(not row['legal'] for row in rows)}
    for strategy in ('sa_lp','hybrid'):
        medians=collections.Counter(); seeds=collections.Counter()
        for cases in grouped.values():
            base=cases['current'][0]['score']; values=[r['score'] for r in cases[strategy]]
            median=statistics.median(values)
            medians['better' if median<base else 'worse' if median>base else 'equal']+=1
            for value in values:seeds['better' if value<base else 'worse' if value>base else 'equal']+=1
        summary[strategy]={'median_cases':dict(medians),'seed_runs':dict(seeds)}
    summary['long']={}
    for row in datasets['long']:
        case=summary['long'].setdefault(normalized(row['input']),{})
        case.setdefault(row['strategy'],[]).append(row['score'])
    summary['metadata']={name:json.loads((getattr(args,name)/'metadata.json').read_text()) for name in datasets}
    for value in summary['metadata'].values():
        if 'inputs' in value:value['inputs']=list(map(normalized,value['inputs']))
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n')
    print(json.dumps({k:v for k,v in summary.items() if k!='metadata'},indent=2,ensure_ascii=False))


if __name__=='__main__':main()
