# GLHello: Mac OS 9 test app for the PVMAC v2 GL stream (PowerPC, Retro68).
# Needs glhello_gen.h, made by gen-glhello-header.sh (see build-ndrv.yml).
cmake_minimum_required(VERSION 3.9)
project(GLHello C)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/glhello_gen.h)
  message(FATAL_ERROR "glhello_gen.h missing: run gen-glhello-header.sh first")
endif()
add_application(GLHello GLHello.c)
