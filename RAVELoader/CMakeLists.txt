# UTM RAVE Loader: Mac OS 9 CFM extension that registers the "UTM RAVE" engine
# (UTMRAVE*.c, drawing through our OpenGLLibrary) or Apple's Classic RAVE
# engine with the QuickDraw 3D RAVE manager (PowerPC, Retro68). No generated
# inputs. Recipe after ../OpenGLLibrary/CMakeLists.txt.
cmake_minimum_required(VERSION 3.9)
project(UTMRAVELoader C)

add_library(UTMRAVELoader SHARED
  UTMRAVELoader.c UTMRAVELog.c UTMRAVEGL.c UTMRAVEEngine.c UTMRAVEContext.c UTMRAVEDraw.c UTMRAVETexture.c)
target_compile_options(UTMRAVELoader PRIVATE -Os -fno-strict-aliasing
  -fno-delete-null-pointer-checks --param=min-pagesize=0 -Wall -Wno-multichar)
set_target_properties(UTMRAVELoader PROPERTIES LINK_FLAGS -Wl,-bE:${CMAKE_CURRENT_SOURCE_DIR}/UTMRAVELoader.exp)

# XCOFF -> PEF; then the loader header gets init/term/no-main (set-pef-init-term.py).
add_custom_command(
  OUTPUT UTMRAVELoader.pef
  COMMAND ${MAKE_PEF} libUTMRAVELoader.so -o UTMRAVELoader.pef
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/set-pef-init-term.py UTMRAVELoader.pef
  DEPENDS UTMRAVELoader ${CMAKE_CURRENT_SOURCE_DIR}/set-pef-init-term.py)

if(REZ_INCLUDE_PATH)
  set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()
# Data fork = PEF, resource fork = cfrg; type 'shlb', creator 'tnsl'. The
# Finder name is "UTM RAVE Loader" (the .bin is renamed by the user / kit).
add_custom_command(
  OUTPUT UTMRAVELoader.bin UTMRAVELoader.dsk
  COMMAND ${REZ} ${REZ_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/UTMRAVELoader.r ${REZ_INCLUDE_FLAG}
          -o "UTM RAVE Loader.bin" --cc "UTM RAVE Loader.dsk"
          -t shlb -c tnsl --data UTMRAVELoader.pef
  COMMAND cp "UTM RAVE Loader.bin" UTMRAVELoader.bin
  COMMAND cp "UTM RAVE Loader.dsk" UTMRAVELoader.dsk
  DEPENDS UTMRAVELoader.pef ${CMAKE_CURRENT_SOURCE_DIR}/UTMRAVELoader.r)
add_custom_target(UTMRAVELoader_SHLB ALL DEPENDS UTMRAVELoader.bin)
