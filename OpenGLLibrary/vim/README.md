# VirtualIntelMac generator patch (PPC element widths)

`vim-ppc-element-width.patch` patches `Source/scripts/generate-passthrough-opengl.py`
of VirtualIntelMac (MIT, https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon,
pinned 2861da9e265e557aef90319e4451487ed1fed553). Apply with
`git -C <vim> apply vim-ppc-element-width.patch`. The patch adds MIT-licensed
VirtualIntelMac-derived code; the generator header notice applies.

With `VIM_GEN_ELEMENT_WIDTH` unset the generator output is byte-identical to the
original (checked in CI, job `opengl-codegen`). With `VIM_GEN_ELEMENT_WIDTH=1` it
also writes `direct-element-width.inc` and `opengl-codegen-ppc.json`, and the
client encoder calls `DG*W` helpers carrying the payload element width.
Never run the generator on the Mac; CI only.
