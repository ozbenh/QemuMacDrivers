#!/usr/bin/env python3
"""Set the PEF loader header of a MakePEF container for a CFM library with
initialization and termination routines, and "no main symbol".

Retro68's MakePEF writes initSection/termSection = -1 and has no option to
name an init routine, so the routines are exported (UTMRAVEInit / UTMRAVETerm,
UTMRAVELoader.exp), and their export entries (section, offset of the TVector)
are copied into the header: mainSection -1 / mainOffset 0, initSection /
initOffset, termSection / termOffset.
usage: set-pef-init-term.py FILE.pef"""
import struct, sys

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
K = off + hashoff + 4 * n
S = K + 4 * nexp
found = {}
for i in range(nexp):
    ln = struct.unpack('>I', d[K + 4 * i:K + 4 * i + 4])[0] >> 16
    no, offs, sect = struct.unpack('>IIh', d[S + 10 * i:S + 10 * i + 10])
    name = bytes(d[off + stroff + (no & 0xffffff):off + stroff + (no & 0xffffff) + ln]).decode('latin1')
    found[name] = (sect, offs)
for need in ('UTMRAVEInit', 'UTMRAVETerm'):
    assert need in found, 'export %s missing' % need
isec, ioff = found['UTMRAVEInit']
tsec, toff = found['UTMRAVETerm']
struct.pack_into('>iIiIiI', d, off, -1, 0, isec, ioff, tsec, toff)
open(p, 'r+b').write(d)
print('%s: init section %d offset %#x, term section %d offset %#x, main none' % (p, isec, ioff, tsec, toff))
