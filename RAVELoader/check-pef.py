#!/usr/bin/env python3
"""Check UTMRAVELoader.pef / .bin: loader header init and term section/offset
are set and point at TVectors (two words, code and TOC pointers) inside the
data section, no main symbol, MacBinary type 'shlb' creator 'tnsl', cfrg
resource present. Exits non-zero on a mismatch.
usage: check-pef.py <UTMRAVELoader.pef> <UTMRAVELoader.bin>"""
import struct
import sys

pef = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
ok = True


def fail(msg):
    global ok
    print("::error::" + msg)
    ok = False


assert pef[:8] == b"Joy!peff" and pef[8:12] == b"pwpc", "not a PowerPC PEF"
nsec = struct.unpack(">H", pef[32:34])[0]
secs = []
loader = None
for i in range(nsec):
    s = pef[40 + 28 * i:68 + 28 * i]
    name_off, dflt, tot, unpacked, packed, cont_off, kind = struct.unpack(">iIIIIIB", s[:25])
    secs.append((kind, unpacked, packed, cont_off, tot))
    if kind == 4:
        loader = cont_off
    print("section %d: kind %d total %d unpacked %d packed %d offset %d" % (i, kind, tot, unpacked, packed, cont_off))
if loader is None:
    fail("no loader section")
    sys.exit(1)
msec, moff, isec, ioff, tsec, toff = struct.unpack(">iIiIiI", pef[loader:loader + 24])
print("loader header: main %d/%#x init %d/%#x term %d/%#x" % (msec, moff, isec, ioff, tsec, toff))
if msec != -1:
    fail("mainSection is %d, want -1" % msec)
for what, sec, o in (("init", isec, ioff), ("term", tsec, toff)):
    if not 0 <= sec < nsec or secs[sec][0] not in (1, 2):  # unpacked / pattern-initialized data
        fail("%s section %d is not a data section" % (what, sec))
        continue
    kind, unpacked, packed, cont_off, tot = secs[sec]
    if o + 8 > tot or o % 4:
        fail("%s TVector %#x outside the data section (total %d)" % (what, o, tot))
        continue
    print("%s TVector at section %d offset %#x (data section total %d)" % (what, sec, o, tot))
if (isec, ioff) == (tsec, toff):
    fail("init and term are the same TVector")
ftype, creator = b[65:69], b[69:73]
dlen, rlen = struct.unpack(">II", b[83:91])
print("MacBinary: type %r creator %r data fork %d resource fork %d" % (ftype, creator, dlen, rlen))
if ftype != b"shlb" or creator != b"tnsl":
    fail("MacBinary type/creator must be shlb/tnsl")
if dlen != len(pef) or not rlen:
    fail("MacBinary forks wrong")
elif b"cfrg" not in b[128 + ((dlen + 127) // 128) * 128:]:
    fail("no cfrg resource in the resource fork")
print("OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
