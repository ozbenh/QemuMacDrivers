#!/usr/bin/env python3
"""Set the termination routine of OpenGLLibrary.pef in the PEF loader header.

Retro68's MakePEF writes termSection = -1 and has no option to name a
termination routine, so the routine is exported (UTMGLTerm, in agl-exports.txt)
and its export entry (section, offset of the TVector) is copied into the
header's termSection / termOffset. The other header fields (main, init) are
left as they are: run this after patch-pef-main.sh and pef-fix-exports.py.
Derived from RAVELoader/set-pef-init-term.py.
usage: set-pef-term.py FILE.pef"""
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
assert 'UTMGLTerm' in found, 'export UTMGLTerm missing'
tsec, toff = found['UTMGLTerm']
struct.pack_into('>iI', d, off + 16, tsec, toff)
open(p, 'r+b').write(d)
print('%s: term section %d offset %#x' % (p, tsec, toff))
