"""make_route.py START HOPS > route.txt - build a navigation walk for bench.sh.

Adjacency = union of each node's nav_*_to_wX_zYY_nZZZ resources and the
(stale, unused by the engine) nodedb.ndb, restricted to nodes that exist.
START is "w z n", e.g. "1 1 20". Greedy walk preferring unvisited nodes in the same world.
"""
import sys, os, re, struct, random, collections
sys.path.insert(0, os.path.dirname(__file__))
from m4b import parse_fat
from paths import GAME_DATA

f = open(os.path.join(GAME_DATA, 'data.m4b'), 'rb')
outer, _, _ = parse_fat(f.read(1 << 20))
adj = collections.defaultdict(set); nodes = set(); ndb = None
for path, size, off in outer:
    if path == 'nodedb.ndb': f.seek(off); ndb = f.read(size)
    m = re.match(r'global/w(\d)/z(\d\d)/n(\d\d\d)/.*\.m4b$', path)
    if not m: continue
    key = tuple(map(int, m.groups())); nodes.add(key)
    f.seek(off)
    for name, _, _ in parse_fat(f.read(min(size, 1 << 16)))[0]:
        mm = re.search(r'nav_\w*?_to_w(\d)_z(\d\d)_n(\d\d\d)', name)
        if mm: adj[key].add(tuple(map(int, mm.groups())))
if ndb:   # u32 count; per entry: u32 n, key id, n neighbour ids; id = (u8 world, u8 zone, u16 node)
    p = 12
    for _ in range(struct.unpack_from('<I', ndb, 8)[0]):
        n = struct.unpack_from('<I', ndb, p)[0]; p += 4
        ids = [struct.unpack_from('<BBH', ndb, p + 4 * i) for i in range(n + 1)]; p += 4 * (n + 1)
        adj[ids[0]].update(ids[1:])
graph = collections.defaultdict(set)
for k, v in adj.items():
    for x in v:
        if k in nodes and x in nodes and x != k: graph[k].add(x); graph[x].add(k)

cur = tuple(map(int, sys.argv[1].split())) if len(sys.argv) > 1 else (1, 1, 20)
hops = int(sys.argv[2]) if len(sys.argv) > 2 else 30
random.seed(4); route = [cur]; seen = {cur}
for _ in range(hops):
    nb = sorted(x for x in graph[cur] if x[0] == cur[0])
    new = [x for x in nb if x not in seen]
    if not nb: break
    cur = new[0] if new else random.choice(nb); seen.add(cur); route.append(cur)
print('\r\n'.join(f'{w} {z} {n}' for w, z, n in route))
