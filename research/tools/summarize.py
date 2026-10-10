"""summarize.py trace.log - per-context-load breakdown: wall time, LoadingRequest time, and aggregated stats."""
import re, sys, collections
lines = open(sys.argv[1], encoding='latin1').read().splitlines()
phases = []  # (start_ms, ctx, label)
cur = None; agg = collections.defaultdict(lambda: [0, 0.0]); reqs = []
def flush(end):
    if cur is None: return
    t0, ctx = cur
    tot = sum(d for _, d in reqs)
    print(f'== ctx {ctx}: InitContext@{t0:.0f}ms -> ActivateContext@{end:.0f}ms  wall={end-t0:.1f}ms  requests={len(reqs)} req_time={tot:.1f}ms')
    for k, (n, t) in sorted(agg.items(), key=lambda kv: -kv[1][1]):
        print(f'     {k:34s} n={n:<6d} total={t:9.1f}ms')
    big = sorted(reqs, key=lambda r: -r[1])[:5]
    print('     slowest requests:', ', '.join(f'{d:.0f}ms@{s:.0f}' for s, d in big))
for i, l in enumerate(lines):
    m = re.match(r'\s*([\d.]+) \[.\]\s+([<>]) (\S+)(.*)', l)
    if m:
        t, d, name, rest = float(m.group(1)), m.group(2), m.group(3), m.group(4)
        if name == 'LoaderManager::InitContext' and d == '>':
            cur = (t, rest.strip()); agg.clear(); reqs.clear()
        elif name == 'LoaderManager::ActivateContext' and d == '<' and cur:
            flush(t); cur = None
        elif name == 'LoadingRequest::Load' and d == '<' and cur:
            reqs.append((t, float(rest.strip().rstrip('ms'))))
        continue
    m = re.match(r'\s+(\S+)\s+n=(\d+)\s+total=\s*([\d.]+)ms', l)
    if m and cur:
        agg[m.group(1)][0] += int(m.group(2)); agg[m.group(1)][1] += float(m.group(3))
