# UTM RAVE Loader and the UTM RAVE engine (Mac OS 9)

`UTM RAVE Loader` is a PowerPC CFM extension (type `shlb`, creator `tnsl`).
The QuickDraw 3D RAVE manager FragPrepares every such file of the Extensions
folder in every process, so the CFM initialization routine runs per process
and registers a RAVE engine. Built only in CI (`.github/workflows/build-ndrv.yml`,
job `rave-loader`, artifact `RAVELoader-os9`, Retro68 + Apple's Universal
Interfaces 3.4).

Which engine is registered (outside Mac OS X Classic, where the manager loads
Classic RAVE itself and the loader does nothing):

| Situation | Engine |
|-----------|--------|
| our `OpenGLLibrary` present (exports `UTMGLTerm`), no control file | **UTM RAVE**: our own RAVE 1.6 engine, drawing through `OpenGLLibrary` (AGL + GL over the paravirtual wire) |
| System Folder contains a file named `UTM RAVE.classic`, or `OpenGLLibrary` is missing / not ours | Apple's **Classic RAVE** (the previous behaviour) |

In both cases every engine that existed before is disabled with
`QAEngineDisable`, so the registered one is the manager's default.

Logging: only when `OpenGLLibrary.debug` exists in the System Folder; lines go
to `UTM RAVE Loader.log` there.

| File | What |
|------|------|
| `UTMRAVELoader.c` | CFM init/term routines, engine selection, manager lookup, disabling the other engines |
| `UTMRAVELog.[ch]` | shared logging, log-once helper, System Folder file test |
| `UTMRAVEGL.[ch]` | table of the GL / AGL entry points, resolved with `FindSymbol` (no link-time import of `OpenGLLibrary`) |
| `UTMRAVEEngine.[ch]` | engine registration data: method table, gestalt, device check, shared AGL objects; **the design notes are in the file comment** |
| `UTMRAVEContext.c` | draw contexts (CGrafPort drawable, clip, state variables, RenderStart/End, clears, notice methods, memory-device read-back) |
| `UTMRAVEDraw.c` | drawing methods; vertex mapping, depth, texture operations and blending rules are in the file comment |
| `UTMRAVETexture.c` | textures, bitmaps, color tables, pixel conversion to RGBA8 |
| `UTMRAVELoader.exp`, `set-pef-init-term.py`, `check-pef.py`, `UTMRAVELoader.r` | export list, PEF init/term header patch, CI check, cfrg resource |

The engine structure follows Apple's "Empty Engine" sample (Tinsel Town, Mike W.
Kelley, 1994-95 Apple Computer, Inc.), extended to the RAVE 1.6 method set.

## UTM RAVE: what it implements

* Engine identity: vendor id `'UTM '` (`0x55544D20`), engine id 1, name "UTM RAVE".
* Devices: main GDevice (16 / 32 bit) and memory devices (ARGB16, ARGB32,
  RGB16, RGB32).
* Draw methods: points, lines, Gouraud and textured triangles, vertex arrays
  (point, line, polyline, tri, strip, fan), triangle meshes, bitmaps (with
  `BitmapScale`), `RenderStart/End/Abort`, `Flush`, `Sync`, `Busy`,
  `SwapBuffers`, `ClearDrawBuffer`, `ClearZBuffer`, notice methods
  (`RenderCompletion`, `ImageBufferInitialize`, `ImageBuffer2DComposite`).
* Textures and bitmaps: ARGB32, RGB32, ARGB16, RGB16, RGB16_565, ARGB16_4444,
  RGB24, RGB8_332, CL4, CL8, ACL16_88, I8, AI16_88 and (bitmaps) Alpha1; mipmaps,
  flipped origin, color tables.
* State: ZFunction (all values), ZBufferMask, ChannelMask,
  Blend (PreMultiply, Interpolate, OpenGL with BlendSrc/Dst), AlphaTest,
  PerspectiveZ, TextureOp (Modulate, Highlight, Decal, Blend), TextureFilter,
  MipmapBias, DontSwap, Width, and the GL tags TextureWrapU/V, TextureMag/MinFilter,
  Scissor*, DepthBG, TextureBorder, TextureEnvColor, LinePattern(+Factor),
  AreaPattern0-31, ColorBG.

Not implemented, and not claimed in gestalt (set once, logged once):
fog, multitexture, antialiasing, CSG, chroma keying, Z-sorted rendering,
`Access*` (draw buffer, z buffer, texture, bitmap), textures / bitmaps from draw
contexts, scaled draw contexts, `kQATagGL_DrawBuffer`, `kQATexture_NoCopy` /
`Lock` (textures are always copied to the host at `QATextureNew`).
