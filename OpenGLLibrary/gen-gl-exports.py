#!/usr/bin/env python3
"""Generate the gl* export wrappers of the OS 9 OpenGLLibrary.

Input: the opengl-codegen-ppc artifact's ppc/ directory (VirtualIntelMac's
generated client code with element widths, MIT) and the name lists
gl-exports.txt / agl-exports.txt. Output (to <outdir>):
  gl_generated.c      the generated client function of every listed name
  OpenGLLibrary.exp   the CFM export list (agl + gl names + the glc_cc data symbol)
  gl_dispatch.[ch]    gGLIDispatch[974]: OS X's GLIFunctionDispatch (the artifact's
                      passthrough-opengl-gli-map.json) in slot order, for Apple's GLU
                      which calls through glc_cc; unexported slots get a logging stub
  + copies of the generated headers/includes the build needs.

Adding an export = adding its name to gl-exports.txt (`glFooARB = glFoo`
exports glFoo's encoder under another name) or agl-exports.txt. Slots that VIM
writes by hand ("custom-client" in the JSON) must be defined in glcustom.c.
Fails with a message on anything inconsistent.

usage: gen-gl-exports.py <codegen ppc dir> <outdir>
"""
import json
import pathlib
import re
import shutil
import sys

here = pathlib.Path(__file__).resolve().parent
src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)


def names(path):
    entries = []
    for line in path.read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        if "=" in line:
            alias, base = [s.strip() for s in line.split("=", 1)]
        else:
            alias = base = line
        entries.append((alias, base))
    return entries


def fail(message):
    print("::error::gen-gl-exports: " + message)
    sys.exit(1)


# Apple's OpenGLUtility imports glc_cc (the current context pointer) as data.
DATA_EXPORTS = ["glc_cc"]

gl_entries = names(here / "gl-exports.txt")
agl_entries = [a for a, _ in names(here / "agl-exports.txt")]
slots = {}
for s in json.loads((src / "opengl-codegen-ppc.json").read_text())["slots"]:
    if s["gl_name"] and s["state"] in ("generated", "custom-client"):
        slots.setdefault(s["gl_name"], s)

# hand-written client functions
custom_text = (here / "glcustom.c").read_text()
custom = set(re.findall(r"^[A-Za-z_][\w \*]*?\b(gl\w+)\(", custom_text, re.M))

# every function of the generated client, by name
inc = (src / "direct-client-generated.inc").read_text().split("\n")
functions, i = {}, 0
while i < len(inc):
    m = re.match(r"^[A-Za-z_].*?\b(gl\w+)\(.*\) \{$", inc[i])
    if m:
        j = i
        while inc[j] != "}":
            j += 1
        functions.setdefault(m.group(1), (i, j))
        i = j
    i += 1

body, protos, exports, seen = [], [], [], set()
local = []
for alias, base in gl_entries:
    if alias in seen:
        fail("duplicate export " + alias)
    seen.add(alias)
    slot = slots.get(base)
    if not slot and alias == base and base in custom:
        # hand-written in glcustom.c, no slot in VIM's generator (e.g. glLockArraysEXT)
        local.append(alias)
        exports.append(alias)
        continue
    if not slot:
        fail("%s: no usable slot named %s in opengl-codegen-ppc.json" % (alias, base))
    if not slot.get("supported", True):
        fail("%s: slot %s is not supported by the host executor" % (alias, base))
    if slot["state"] == "custom-client":
        if alias != base or base not in custom:
            fail("%s is a custom-client slot: define %s in glcustom.c" % (alias, base))
    else:
        if base not in functions:
            fail("%s: generated client has no function %s" % (alias, base))
        a, b = functions[base]
        lines = inc[a:b + 1]
        if alias != base:
            # a self-call (staged large uploads re-enter the encoder) follows the alias
            lines = [re.sub(r"\b%s\(" % base, alias + "(", l) for l in lines]
        if alias == "glGetIntegerv":
            # verbose log (dg_log.c) of the pname queried
            pm = re.match(r"^[^(]*\(\s*\w+\s+(\w+)\s*,", lines[0])
            if not pm:
                fail("glGetIntegerv: cannot find the pname parameter in " + lines[0])
            lines.insert(1, '\tDGVLog("glGetIntegerv(%%lx)", (unsigned long)%s);' % pm.group(1))
        protos.append(re.sub(r" \{$", ";", lines[0]))
        body.append("/* %s -> slot %d %s */" % (alias, slot["opcode"], base))
        body.extend(lines)
    exports.append(alias)

agl_text = (here / "agl.c").read_text()
for name in agl_entries:
    if not re.search(r"^[A-Za-z_][\w \*]*?\b%s\(" % name, agl_text, re.M):
        fail("agl export %s is not defined in agl.c" % name)
    exports.append(name)

# data exports (PEF class 1): MakePEF marks every non-descriptor symbol as data
for name in DATA_EXPORTS:
    if not re.search(r"^[A-Za-z_][\w \*]*?\b%s\b" % name, agl_text, re.M):
        fail("data export %s is not defined in agl.c" % name)
    exports.append(name)


# --- GLIFunctionDispatch table behind glc_cc (see agl.c)
GLI_SLOTS = 974
GLI_ORTHO = (189, "ortho", "glOrtho")
gli = json.loads((src / "passthrough-opengl-gli-map.json").read_text())["table"]
if len(gli) != GLI_SLOTS or [e["index"] for e in gli] != list(range(GLI_SLOTS)):
    fail("passthrough-opengl-gli-map.json: expected %d slots in index order, got %d" % (GLI_SLOTS, len(gli)))
e = gli[GLI_ORTHO[0]]
if e["field"] != GLI_ORTHO[1] or e["direct"] != [GLI_ORTHO[2]]:
    fail("GLI slot %d is %s -> %s, expected %s -> %s (dispatch layout drifted)"
         % (GLI_ORTHO[0], e["field"], e["direct"], GLI_ORTHO[1], GLI_ORTHO[2]))
if len({e["field"] for e in gli}) != GLI_SLOTS:
    fail("duplicate field names in the GLI map")

glmin_types = set(re.findall(r"typedef\s[^;]*?\b(GL\w+);", (here / "glmin.h").read_text()))
EXTRA_TYPES = {
    "GLchar": "char", "GLcharARB": "char", "GLhandleARB": "unsigned int",
    "GLint64": "long long", "GLint64EXT": "long long",
    "GLuint64": "unsigned long long", "GLuint64EXT": "unsigned long long",
    "GLintptr": "long", "GLintptrARB": "long", "GLsizeiptr": "long", "GLsizeiptrARB": "long",
    "GLsync": "void *", "GLIContext": "void *",
}
exported = {a for a, _ in gl_entries} | set(local)
by_base = {}
for a, b in gl_entries:
    by_base.setdefault(b, a)
need = set()
for e in gli:
    need |= set(re.findall(r"\bGL\w+", e["parameters"] + " " + e["result"]))
need -= glmin_types
unknown = sorted(need - set(EXTRA_TYPES))
if unknown:
    fail("GLI map uses types without a definition: " + " ".join(unknown))

disp = ["/* Generated by gen-gl-exports.py from passthrough-opengl-gli-map.json (OS X's",
        " * GLIFunctionDispatch order). Do not edit. */",
        '#include "glmin.h"', '#include "dg_client.h"', '#include "gl_dispatch.h"', ""]
for t in sorted(need):
    disp.append("typedef %s %s;" % (EXTRA_TYPES[t], t))
disp += ["", "static void GLIStub(int slot)", "{", "\tstatic unsigned char seen[%d];" % GLI_SLOTS,
         "\tif (!seen[slot]) {", "\t\tseen[slot] = 1;",
         '\t\tDGLog("glc_cc: unimplemented dispatch slot %d", slot);', "\t}", "}", ""]
entries, wired, stubbed = [], 0, []
for e in gli:
    n, f = e["index"], e["field"]
    target = None
    for d in e["direct"]:
        if d in exported:
            target = d
        elif d in by_base:
            target = by_base[d]
        if target:
            break
    if not target:
        disp.append("static long gli_%s(void) { GLIStub(%d); return 0; }" % (f, n))
        stubbed.append(f)
    else:
        if any(c in e["parameters"] for c in "()["):
            fail("GLI slot %d %s: parameter list is not simple: %s" % (n, f, e["parameters"]))
        params = [p.strip() for p in e["parameters"].split(",")]
        if not re.fullmatch(r"GLIContext\s+ctx", params[0]):
            fail("GLI slot %d %s: first parameter is not 'GLIContext ctx'" % (n, f))
        rest = [] if params[1:] == ["void"] else params[1:]
        args = []
        for p in rest:
            m = re.search(r"(\w+)\s*$", p)
            if not m or m.group(1) == "void":
                fail("GLI slot %d %s: cannot find the parameter name in '%s'" % (n, f, p))
            args.append(m.group(1))
        disp.append("extern %s %s(%s);" % (e["result"], target, ", ".join(rest) if rest else "void"))
        disp.append("static %s gli_%s(%s) { (void)ctx; %s%s(%s); }" % (
            e["result"], f, e["parameters"], "" if e["result"] == "void" else "return ",
            target, ", ".join(args)))
        wired += 1
    entries.append("\t(GLIFn)gli_%s,\t/* %d */" % (f, n))
disp += ["", "const GLIFn gGLIDispatch[GLI_SLOTS] = {"] + entries + ["};", ""]
(out / "gl_dispatch.c").write_text("\n".join(disp))
(out / "gl_dispatch.h").write_text(
    "/* Generated by gen-gl-exports.py. Do not edit. */\n#ifndef GLDISPATCH_INCLUDED\n#define GLDISPATCH_INCLUDED\n"
    "#define GLI_SLOTS %d\n#define GLI_SLOT_ORTHO %d\ntypedef void (*GLIFn)(void);\n"
    "extern const GLIFn gGLIDispatch[GLI_SLOTS];\n#endif\n" % (GLI_SLOTS, GLI_ORTHO[0]))
print("GLI dispatch: %d slots, %d wired, %d stubbed" % (GLI_SLOTS, wired, len(stubbed)))
print("GLI stubbed fields: " + " ".join(stubbed))

(out / "gl_generated.c").write_text(
    "/* Generated by gen-gl-exports.py from VirtualIntelMac's direct-client-generated.inc\n"
    " * (MIT, nfzerox, pinned 2861da9e; element-width patch in OpenGLLibrary/vim). Do not edit. */\n"
    '#include "dg_client.h"\n\n/* prototypes: some encoders call others (glGetFloatv -> glGetIntegerv) */\n'
    + "\n".join(protos) + "\n\n" + "\n".join(body) + "\n")
(out / "OpenGLLibrary.exp").write_text("\n".join(exports) + "\n")

for f in ("direct-layout.h", "direct-get-counts.inc", "server-state-query-counts.inc",
          "additional-state-query-counts.inc", "direct-element-width.inc", "glenum-values.h"):
    if not (src / f).exists():
        fail("missing %s in %s" % (f, src))
    shutil.copy(src / f, out / f)

# Every GL_ name the sources use must have a number.
defined = set(re.findall(r"^#define (GL_\w+) ", (out / "glenum-values.h").read_text(), re.M))
used = set()
for p in list(here.glob("*.[ch]")) + list(out.glob("*.[ch]")) + list(out.glob("*.inc")):
    if p.name == "glenum-values.h":
        continue
    used |= set(re.findall(r"\bGL_[A-Z][A-Z0-9_]*\b", p.read_text()))
missing = sorted(used - defined)
if missing:
    fail("GL_ names without a value in glenum-values.h: " + " ".join(missing))

custom_used = sorted([a for a, b in gl_entries if b in slots and slots[b]["state"] == "custom-client"] + local)
print("exports: %d gl (%d generated, %d custom-client: %s), %d agl"
      % (len(gl_entries), len(gl_entries) - len(custom_used), len(custom_used),
         " ".join(custom_used), len(agl_entries)))
