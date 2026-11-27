# OpenGLLibrary: Mac OS 9 CFM replacement for Apple's OpenGLLibrary (PowerPC,
# Retro68). Needs gen/ from gen-gl-exports.py (see build-ndrv.yml, job
# opengl-library). Recipe after Retro68's Samples/SharedLibrary.
cmake_minimum_required(VERSION 3.9)
project(OpenGLLibrary C)
set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/gen)
if(NOT EXISTS ${GEN}/OpenGLLibrary.exp)
  message(FATAL_ERROR "gen/ missing: run gen-gl-exports.py first")
endif()

add_library(OpenGLLibrary SHARED
  dg_client.c dg_arrays.c dg_extra.c dg_transport.c dg_log.c dg_trace.c agl.c glcustom.c ${GEN}/gl_generated.c ${GEN}/gl_dispatch.c)
target_include_directories(OpenGLLibrary PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN})
# -fno-strict-aliasing: payload bytes are reinterpreted. min-pagesize=0 and
# no null-pointer check deletion: classic Mac OS has valid low memory.
target_compile_options(OpenGLLibrary PRIVATE -Os -fno-strict-aliasing
  -fno-delete-null-pointer-checks --param=min-pagesize=0
  -Wall -Wno-multichar -Wno-pointer-sign -Wno-unused-function -Wno-error=int-conversion)
set_target_properties(OpenGLLibrary PROPERTIES LINK_FLAGS -Wl,-bE:${GEN}/OpenGLLibrary.exp)

# XCOFF -> PEF. MakePEF leaves the container versions 0: set current 1.2.0
# (0x01208000, pef-current-version.bin) at header offset 28; oldDefVersion (20) and oldImpVersion (24)
# stay 0 so any importer that links with a version >= 0 accepts it.
# The loader header's mainSection/mainOffset become -1/0 (no main symbol).
# MakePEF's export hash chains are regrouped (pef-fix-exports.py) so CFM FindSymbol by name works.
# The loader header's termSection/termOffset name the exported UTMGLTerm (set-pef-term.py): CFM
# runs it when a process that loaded the library quits (it releases that process' host contexts).
add_custom_command(
  OUTPUT OpenGLLibrary.pef
  COMMAND ${MAKE_PEF} libOpenGLLibrary.so -o OpenGLLibrary.pef
  COMMAND dd if=${CMAKE_CURRENT_SOURCE_DIR}/pef-current-version.bin of=OpenGLLibrary.pef bs=1 seek=28 conv=notrunc
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/patch-pef-main.sh OpenGLLibrary.pef
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/pef-fix-exports.py OpenGLLibrary.pef
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/set-pef-term.py OpenGLLibrary.pef
  DEPENDS OpenGLLibrary ${CMAKE_CURRENT_SOURCE_DIR}/pef-current-version.bin
          ${CMAKE_CURRENT_SOURCE_DIR}/patch-pef-main.sh ${CMAKE_CURRENT_SOURCE_DIR}/pef-fix-exports.py
          ${CMAKE_CURRENT_SOURCE_DIR}/set-pef-term.py)

if(REZ_INCLUDE_PATH)
  set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()
# Data fork = PEF, resource fork = cfrg; file type 'shlb'. MacBinary output.
add_custom_command(
  OUTPUT OpenGLLibrary.bin OpenGLLibrary.dsk
  COMMAND ${REZ} ${REZ_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLLibrary.r ${REZ_INCLUDE_FLAG}
          -o OpenGLLibrary.bin --cc OpenGLLibrary.dsk
          -t shlb -c "????" --data OpenGLLibrary.pef
  DEPENDS OpenGLLibrary.pef ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLLibrary.r)
add_custom_target(OpenGLLibrary_SHLB ALL DEPENDS OpenGLLibrary.bin)
