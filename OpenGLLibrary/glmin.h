/* Minimal OpenGL 1.x type definitions for the OpenGLLibrary build. Written
 * from the public OpenGL specification; the GL_ enum values come from
 * glenum-values.h, generated in CI (numbers only) by gen-glenums.py. */
#ifndef GLMIN_H
#define GLMIN_H
#include <stdint.h>
#include <stddef.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;

#include "glenum-values.h"
#endif
