/* Run-time table of the OpenGLLibrary entry points the UTM RAVE engine uses.
 *
 * The engine does not link against OpenGLLibrary: UTMRAVEGLResolve looks every
 * name up with FindSymbol in the library's connection, so the loader still
 * loads (and falls back to Classic RAVE) when the library is absent or is not
 * ours. Types and constants are written from the public OpenGL 1.x / AGL
 * specifications; only the values the engine needs are defined. */
#ifndef UTMRAVEGL_H
#define UTMRAVEGL_H
#include <MacTypes.h>
#include <Quickdraw.h>
#include <CodeFragments.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned short GLushort;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef struct __AGLContextRec *AGLContext;
typedef struct __AGLPixelFormatRec *AGLPixelFormat;

/* primitives */
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
/* capabilities */
#define GL_LINE_STIPPLE 0x0B24
#define GL_POLYGON_STIPPLE 0x0B42
#define GL_DEPTH_TEST 0x0B71
#define GL_ALPHA_TEST 0x0BC0
#define GL_BLEND 0x0BE2		/* also the GL_BLEND texture environment mode */
#define GL_DITHER 0x0BD0
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE_2D 0x0DE1
#define GL_COLOR_SUM 0x8458
#define GL_DEPTH_CLAMP 0x864F	/* ARB_depth_clamp, as OpenGLLibrary's agl.c */
/* comparison functions */
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
/* blend factors */
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
/* buffers, matrices */
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
/* textures */
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP 0x2900
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2104
#define GL_TEXTURE_FILTER_CONTROL 0x8500
#define GL_TEXTURE_LOD_BIAS 0x8501

/* AGL pixel format attributes */
#define AGL_NONE 0
#define AGL_RGBA 4
#define AGL_DOUBLEBUFFER 5
#define AGL_DEPTH_SIZE 12

/* X(return type, name, (parameters)): each entry becomes a member of gGL and
 * a FindSymbol lookup. The prototypes mirror OpenGLLibrary's exports. */
#define UTM_GL_FUNCTIONS(X) \
	X(void, glBegin, (GLenum mode)) \
	X(void, glEnd, (void)) \
	X(void, glVertex2f, (GLfloat x, GLfloat y)) \
	X(void, glVertex3f, (GLfloat x, GLfloat y, GLfloat z)) \
	X(void, glColor4f, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
	X(void, glSecondaryColor3fEXT, (GLfloat r, GLfloat g, GLfloat b)) \
	X(void, glTexCoord2f, (GLfloat s, GLfloat t)) \
	X(void, glTexCoord4f, (GLfloat s, GLfloat t, GLfloat r, GLfloat q)) \
	X(void, glEnable, (GLenum cap)) \
	X(void, glDisable, (GLenum cap)) \
	X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
	X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
	X(void, glMatrixMode, (GLenum mode)) \
	X(void, glLoadIdentity, (void)) \
	X(void, glOrtho, (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)) \
	X(void, glDepthFunc, (GLenum func)) \
	X(void, glDepthMask, (GLboolean flag)) \
	X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
	X(void, glBlendFunc, (GLenum src, GLenum dst)) \
	X(void, glAlphaFunc, (GLenum func, GLfloat ref)) \
	X(void, glClear, (GLbitfield mask)) \
	X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
	X(void, glClearDepth, (GLdouble depth)) \
	X(void, glBindTexture, (GLenum target, GLuint name)) \
	X(void, glGenTextures, (GLsizei n, GLuint *names)) \
	X(void, glDeleteTextures, (GLsizei n, const GLuint *names)) \
	X(void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
	X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
	X(void, glTexParameterfv, (GLenum target, GLenum pname, const GLfloat *params)) \
	X(void, glTexEnvi, (GLenum target, GLenum pname, GLint param)) \
	X(void, glTexEnvf, (GLenum target, GLenum pname, GLfloat param)) \
	X(void, glTexEnvfv, (GLenum target, GLenum pname, const GLfloat *params)) \
	X(void, glLineWidth, (GLfloat width)) \
	X(void, glPointSize, (GLfloat size)) \
	X(void, glLineStipple, (GLint factor, GLushort pattern)) \
	X(void, glPolygonStipple, (const GLubyte *mask)) \
	X(void, glRasterPos2f, (GLfloat x, GLfloat y)) \
	X(void, glDrawPixels, (GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)) \
	X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)) \
	X(void, glFlush, (void)) \
	X(void, glFinish, (void)) \
	X(AGLPixelFormat, aglChoosePixelFormat, (const void *gdevs, GLint ndev, const GLint *attribs)) \
	X(void, aglDestroyPixelFormat, (AGLPixelFormat pix)) \
	X(AGLContext, aglCreateContext, (AGLPixelFormat pix, AGLContext share)) \
	X(GLboolean, aglDestroyContext, (AGLContext ctx)) \
	X(GLboolean, aglSetCurrentContext, (AGLContext ctx)) \
	X(GLboolean, aglSetDrawable, (AGLContext ctx, CGrafPtr draw)) \
	X(void, aglSwapBuffers, (AGLContext ctx)) \
	X(GLenum, aglGetError, (void))

struct UTMGL {
#define UTM_GL_MEMBER(ret, name, args) ret (*name) args;
	UTM_GL_FUNCTIONS(UTM_GL_MEMBER)
#undef UTM_GL_MEMBER
};

extern struct UTMGL gGL;

/* Looks every entry point up in `lib`. Returns 1 when all were found; else
 * logs each missing name and returns 0 (the table must not be used). */
int UTMRAVEGLResolve(CFragConnectionID lib);

#endif
