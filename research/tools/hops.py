"""hops.py trace.log - per navigation hop: click->activate latency, long frames, heavy stats."""
import re, sys, collections
L = open(sys.argv[1], encoding='latin1').read().splitlines()
hops = []; cur = None
for l in L:
    m = re.match(r'\s*([\d.]+) BENCH hop (\d+) -> (.*)', l)
    if m:
        cur = dict(t=float(m.group(1)), dest=m.group(3), act=None, frames=[], stats=collections.Counter(), cnt=collections.Counter(), events=[])
        hops.append(cur); continue
    if not cur: continue
    m = re.match(r'\s*([\d.]+)\s+frame ([\d.]+)ms', l)
    if m: cur['frames'].append(float(m.group(2))); continue
    m = re.match(r'\s*([\d.]+) \[(.)\]\s+([<>]) (\S+)(.*)', l)
    if m:
        t, d, name = float(m.group(1)), m.group(3), m.group(4)
        if name == 'LoaderManager::ActivateContext' and d == '<' and cur['act'] is None: cur['act'] = t
        if d == '>' and name in ('ai::ContextPrefetch', 'NodePrefetchStrategy::OnRequestContext', 'LoaderManager::InitContext'):
            cur['events'].append(f'{name.split("::")[-1]}{m.group(5).strip()}@+{t-cur["t"]:.0f}')
        continue
    m = re.match(r'\s+(\S+)\s+n=(\d+)\s+total=\s*([\d.]+)ms', l)
    if m: cur['stats'][m.group(1)] += float(m.group(3)); cur['cnt'][m.group(1)] += int(m.group(2))
lat = []
for i, h in enumerate(hops):
    a = h['act'] - h['t'] if h['act'] else float('nan'); lat.append(a)
    fr = h['frames']
    top = ', '.join(f'{k}={v:.0f}ms/{h["cnt"][k]}' for k, v in h['stats'].most_common(4))
    print(f'hop {i+1:2d} {h["dest"]:14s} click->active={a:7.0f}ms  long_frames={len(fr):3d} sum={sum(fr):6.0f}ms max={max(fr) if fr else 0:6.0f}ms | {top}')
    if '-v' in sys.argv: print('        ', '; '.join(h['events']))
v = sorted(x for x in lat if x == x)
print(f'\nclick->active: median={v[len(v)//2]:.0f}ms mean={sum(v)/len(v):.0f}ms max={v[-1]:.0f}ms')
