#!/usr/bin/env python3
"""Inspect OpenGLLibrary.pef / .bin: container versions, imported libraries,
exported symbols (compared with OpenGLLibrary.exp), MacBinary type and
resource fork. Exits non-zero on a mismatch.

Also asserts that every name of Apple's OpenGLLibrary 1.2 export list
(apple-OpenGLLibrary-exports.txt, next to this script) is exported, and that
glc_cc (Apple's current-context pointer) is a data export (class 1) inside the
data section.

usage: check-pef.py <OpenGLLibrary.pef> <OpenGLLibrary.exp> [OpenGLLibrary.bin]
"""
import os
import struct
import sys

pef = open(sys.argv[1], "rb").read()
want = [l.strip() for l in open(sys.argv[2]) if l.strip()]
ok = True

assert pef[:8] == b"Joy!peff" and pef[8:12] == b"pwpc", "not a PowerPC PEF"
old_def, old_imp, cur, nsec = struct.unpack(">IIIH", pef[20:34])
print("container: oldDefVersion %#x oldImpVersion %#x currentVersion %#x sections %d" % (old_def, old_imp, cur, nsec))
if cur != 0x01208000 or old_def != 0:
    print("::error::unexpected container versions")
    ok = False
loader = None
data_total = data_packed = None
for i in range(nsec):
    s = pef[40 + 28 * i:68 + 28 * i]
    if s[24] == 4:
        loader = struct.unpack(">IIIIII", s[:24])[5]
    elif i == 1:
        # section 1 = data: totalLength (with the zero-filled tail), unpackedLength
        data_total, data_packed = struct.unpack(">II", s[8:16])
assert loader is not None, "no loader section"
L = pef[loader:]
(main_s, main_o, init_s, init_o, term_s, term_o, nlib, nsym, nrel, roff, stroff, hoff, hpow, nexp) = \
    struct.unpack(">iIiIiIIIIIIIII", L[:56])
strings = L[stroff:]


def name(off, length=None):
    if length is None:
        return strings[off:strings.index(b"\0", off)].decode("latin1")
    return strings[off:off + length].decode("latin1")


if main_s != -1 or main_o != 0:
    print("::error::mainSection/mainOffset must be -1/0 (no main), got %d/%#x" % (main_s, main_o))
    ok = False
print("main section %d offset %#x, init %d, term %d/%#x, imported libraries %d, exports %d" % (main_s, main_o, init_s, term_s, term_o, nlib, nexp))
syms = 56 + 24 * nlib
for i in range(nlib):
    no, oimp, cu, cnt, first, opt = struct.unpack(">IIIIIB", L[56 + 24 * i:56 + 24 * i + 21])
    print("  imports %-24s %3d symbols%s" % (name(no), cnt, " (weak)" if opt & 0x40 else ""))
# export table: hash slots, key table (u16 length, u16 hash), symbol entries
keys = hoff + 4 * (1 << hpow)
entries = keys + 4 * nexp
exports = []
classes = []
for i in range(nexp):
    length, _hash = struct.unpack(">HH", L[keys + 4 * i:keys + 4 * i + 4])
    cls_name, offs, sect = struct.unpack(">IIh", L[entries + 10 * i:entries + 10 * i + 10])
    exports.append(name(cls_name & 0xffffff, length))
    classes.append((exports[-1], cls_name >> 24, offs, sect))
missing = sorted(set(want) - set(exports))
extra = sorted(set(exports) - set(want))
print("exports: %d (list wants %d)" % (len(exports), len(want)))
if missing or extra:
    print("::error::export mismatch: missing %s extra %s" % (missing, extra))
    ok = False
for n_, c_, o_, s_ in classes:
    print("  export %-28s class %d section %d offset %#x" % (n_, c_ & 0xf, s_, o_))
tv = [(o_, s_) for n_, c_, o_, s_ in classes if n_ == "UTMGLTerm"]
if not tv or term_s != tv[0][1] or term_o != tv[0][0] or term_s == -1:
    print("::error::loader header termSection/termOffset (%d/%#x) must name the UTMGLTerm export %s" % (term_s, term_o, tv))
    ok = False
DATA_EXPORTS = {"glc_cc"}
bad = [(n_, c_ & 0xf, s_) for n_, c_, o_, s_ in classes if (c_ & 0xf) != 2 and n_ not in DATA_EXPORTS]
if bad:
    print("::error::exports not kTVectorCFragSymbol (2): %s" % bad[:10])
    ok = False
for n_, c_, o_, s_ in classes:
    if n_ in DATA_EXPORTS:
        print("data export %s: class %d section %d offset %#x (data section total %s, initialized %s)"
              % (n_, c_ & 0xf, s_, o_, data_total, data_packed))
        if (c_ & 0xf) != 1 or s_ != 1 or data_total is None or o_ + 4 > data_total:
            print("::error::%s must be a data export (class 1) inside the data section" % n_)
            ok = False
apple_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "apple-OpenGLLibrary-exports.txt")
apple = [l.strip() for l in open(apple_path) if l.strip()]
absent = sorted(set(apple) - set(exports))
print("Apple OpenGLLibrary 1.2 names: %d, exported here: %d" % (len(apple), len(apple) - len(absent)))
if absent or len(apple) != 474 or "glc_cc" not in apple:
    print("::error::Apple export list not fully resolved (%d names in the list): missing %s" % (len(apple), absent))
    ok = False


def hash_word(nm):
    h = 0
    for c in nm.encode("latin1"):
        s = h - (1 << 32) if h & 0x80000000 else h
        h = ((s << 1) - (s >> 16)) & 0xffffffff
        h ^= c
    s = h - (1 << 32) if h & 0x80000000 else h
    return (len(nm) << 16) | ((s ^ (s >> 16)) & 0xffff)


# CFM FindSymbol: slot -> [firstIndex, firstIndex+count) chain, compare full key + name
found = 0
for idx, nm in enumerate(exports):
    key = hash_word(nm)
    slot = (key ^ (key >> hpow)) & ((1 << hpow) - 1)
    (w,) = struct.unpack(">I", L[hoff + 4 * slot:hoff + 4 * slot + 4])
    cnt, first = w >> 18, w & 0x3ffff
    for j in range(first, first + cnt):
        (kj,) = struct.unpack(">I", L[keys + 4 * j:keys + 4 * j + 4])
        if kj == key and classes[j][0] == nm:
            found += 1
            break
print("name lookup: %d/%d" % (found, len(exports)))
if found != len(exports):
    print("::error::export names not reachable through the hash table (run pef-fix-exports.py)")
    ok = False
if "aglGetVersion" not in exports:
    print("::error::aglGetVersion not exported (exact spelling)")
    ok = False
print("export classes: %d x TVector(2), %d x data(1) of %d" % (
    sum(1 for c in classes if (c[1] & 0xf) == 2), sum(1 for c in classes if (c[1] & 0xf) == 1), len(classes)))
if len(sys.argv) > 3:
    b = open(sys.argv[3], "rb").read()
    ftype = b[65:69]
    dlen, rlen = struct.unpack(">II", b[83:91])
    print("MacBinary: type %r creator %r data fork %d resource fork %d" % (ftype, b[69:73], dlen, rlen))
    if ftype != b"shlb" or not rlen or dlen != len(pef):
        print("::error::MacBinary type / forks wrong")
        ok = False
    elif b"cfrg" not in b[128 + ((dlen + 127) // 128) * 128:]:
        print("::error::no cfrg resource in the resource fork")
        ok = False
sys.exit(0 if ok else 1)
