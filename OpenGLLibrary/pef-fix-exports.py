#!/usr/bin/env python3
"""Rebuild the export hash chains of a PEF container in place (Retro68 MakePEF writes the export
key/symbol tables unsorted, so CFM name lookups fail with cfragNoSymbolErr -2802).
usage: pef-fix-exports.py FILE   (FILE = the PEF data fork). Export order changes; nothing refers to
exports by index (relocations only reference imports)."""
import struct, sys

def hash_word(name):
    h = 0
    for c in name:
        s = h - (1 << 32) if h & 0x80000000 else h
        h = ((s << 1) - (s >> 16)) & 0xffffffff
        h ^= c
    s = h - (1 << 32) if h & 0x80000000 else h
    return (len(name) << 16) | ((s ^ (s >> 16)) & 0xffff)

p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
assert d[:12] == b'Joy!peffpwpc', 'not a PowerPC PEF'
off = None
for i in range(struct.unpack('>H', d[32:34])[0]):
    h = d[40 + i * 28:68 + i * 28]
    if h[24] == 4:
        off = struct.unpack('>I', h[20:24])[0]
assert off is not None, 'no loader section'
f = struct.unpack('>14I', d[off:off + 56])
stroff, hashoff, power, nexp = f[10], f[11], f[12], f[13]
n = 1 << power
H, K, S = off + hashoff, off + hashoff + 4 * n, off + hashoff + 4 * n + 4 * nexp
keys = [struct.unpack('>I', d[K + 4 * i:K + 4 * i + 4])[0] for i in range(nexp)]
syms = [bytes(d[S + 10 * i:S + 10 * i + 10]) for i in range(nexp)]
names = []
for i in range(nexp):
    no = struct.unpack('>I', syms[i][:4])[0] & 0xffffff
    names.append(bytes(d[off + stroff + no:off + stroff + no + (keys[i] >> 16)]))
for i in range(nexp):
    assert keys[i] == hash_word(names[i]), f'bad key for {names[i]}'
slot = lambda k: (k ^ (k >> power)) & (n - 1)
order = sorted(range(nexp), key=lambda i: slot(keys[i]))
counts = [0] * n
for i in order:
    counts[slot(keys[i])] += 1
first = 0
for s in range(n):
    struct.pack_into('>I', d, H + 4 * s, (counts[s] << 18) | (first if counts[s] else 0))
    first += counts[s]
for j, i in enumerate(order):
    struct.pack_into('>I', d, K + 4 * j, keys[i])
    d[S + 10 * j:S + 10 * j + 10] = syms[i]
open(p, 'r+b').write(d)
print(f'{p}: {nexp} exports regrouped into {n} hash slots')
