/* AGL 1.2 for the OpenGLLibrary replacement: declarations and constants of
 * the public Mac OS 9 AGL API, written from its documented interface (no
 * Apple SDK header text). Only the subset this library exports. */
#ifndef AGL_H
#define AGL_H
#include <Quickdraw.h>
#include "glmin.h"

typedef struct __AGLContextRec *AGLContext;
typedef struct __AGLPixelFormatRec *AGLPixelFormat;
typedef struct __AGLRendererInfoRec *AGLRendererInfo;
typedef CGrafPtr AGLDrawable;
typedef GDHandle AGLDevice;

/* pixel format attributes */
#define AGL_NONE            0
#define AGL_ALL_RENDERERS   1
#define AGL_BUFFER_SIZE     2
#define AGL_LEVEL           3
#define AGL_RGBA            4
#define AGL_DOUBLEBUFFER    5
#define AGL_STEREO          6
#define AGL_AUX_BUFFERS     7
#define AGL_RED_SIZE        8
#define AGL_GREEN_SIZE      9
#define AGL_BLUE_SIZE       10
#define AGL_ALPHA_SIZE      11
#define AGL_DEPTH_SIZE      12
#define AGL_STENCIL_SIZE    13
#define AGL_ACCUM_RED_SIZE  14
#define AGL_ACCUM_GREEN_SIZE 15
#define AGL_ACCUM_BLUE_SIZE 16
#define AGL_ACCUM_ALPHA_SIZE 17
#define AGL_PIXEL_SIZE      50
#define AGL_MINIMUM_POLICY  51
#define AGL_MAXIMUM_POLICY  52
#define AGL_OFFSCREEN       53
#define AGL_FULLSCREEN      54
#define AGL_SAMPLE_BUFFERS_ARB 55
#define AGL_SAMPLES_ARB     56
#define AGL_AUX_DEPTH_STENCIL 57
#define AGL_RENDERER_ID     70
#define AGL_SINGLE_RENDERER 71
#define AGL_NO_RECOVERY     72
#define AGL_ACCELERATED     73
#define AGL_CLOSEST_POLICY  74
#define AGL_ROBUST          75
#define AGL_BACKING_STORE   76
#define AGL_MP_SAFE         78
#define AGL_WINDOW          80
#define AGL_MULTISCREEN     81
#define AGL_VIRTUAL_SCREEN  82
#define AGL_COMPLIANT       83
#define AGL_PBUFFER         90

/* renderer properties */
#define AGL_BUFFER_MODES    100
#define AGL_MIN_LEVEL       101
#define AGL_MAX_LEVEL       102
#define AGL_COLOR_MODES     103
#define AGL_ACCUM_MODES     104
#define AGL_DEPTH_MODES     105
#define AGL_STENCIL_MODES   106
#define AGL_MAX_AUX_BUFFERS 107
#define AGL_VIDEO_MEMORY    120
#define AGL_TEXTURE_MEMORY  121

/* context options */
#define AGL_SWAP_RECT       200
#define AGL_BUFFER_RECT     202
#define AGL_SWAP_LIMIT      203
#define AGL_COLORMAP_TRACKING 210
#define AGL_COLORMAP_ENTRY  212
#define AGL_RASTERIZATION   220
#define AGL_SWAP_INTERVAL   222
#define AGL_STATE_VALIDATION 230
#define AGL_BUFFER_NAME     231
#define AGL_ORDER_CONTEXT_TO_FRONT 232
#define AGL_CONTEXT_SURFACE_ID 233
#define AGL_CONTEXT_DISPLAY_ID 234
#define AGL_SURFACE_ORDER   235
#define AGL_SURFACE_OPACITY 236
#define AGL_CLIP_REGION     254
#define AGL_FS_CAPTURE_SINGLE 255

/* aglConfigure options */
#define AGL_FORMAT_CACHE_SIZE 501
#define AGL_CLEAR_FORMAT_CACHE 502
#define AGL_RETAIN_RENDERERS 503

/* mode bits for AGL_BUFFER_MODES, AGL_COLOR_MODES, AGL_DEPTH_MODES, ... */
#define AGL_MONOSCOPIC_BIT  0x00000001
#define AGL_STEREOSCOPIC_BIT 0x00000002
#define AGL_SINGLEBUFFER_BIT 0x00000004
#define AGL_DOUBLEBUFFER_BIT 0x00000008
#define AGL_0_BIT           0x00000001
#define AGL_8_BIT           0x00000080
#define AGL_16_BIT          0x00000400
#define AGL_24_BIT          0x00000800
#define AGL_32_BIT          0x00001000
#define AGL_RGB565_BIT      0x00001000
#define AGL_RGB888_BIT      0x00004000
#define AGL_ARGB8888_BIT    0x00008000

/* aglGetError codes */
#define AGL_NO_ERROR        0
#define AGL_BAD_ATTRIBUTE   10000
#define AGL_BAD_PROPERTY    10001
#define AGL_BAD_PIXELFMT    10002
#define AGL_BAD_RENDINFO    10003
#define AGL_BAD_CONTEXT     10004
#define AGL_BAD_DRAWABLE    10005
#define AGL_BAD_GDEV        10006
#define AGL_BAD_STATE       10007
#define AGL_BAD_VALUE       10008
#define AGL_BAD_MATCH       10009
#define AGL_BAD_ENUM        10010
#define AGL_BAD_OFFSCREEN   10011
#define AGL_BAD_FULLSCREEN  10012
#define AGL_BAD_WINDOW      10013
#define AGL_BAD_POINTER     10014
#define AGL_BAD_MODULE      10015
#define AGL_BAD_ALLOC       10016
#define AGL_BAD_CONNECTION  10017

void aglGetVersion(GLint *major, GLint *minor);
AGLPixelFormat aglChoosePixelFormat(const AGLDevice *gdevs, GLint ndev, const GLint *attribs);
void aglDestroyPixelFormat(AGLPixelFormat pix);
GLboolean aglDescribePixelFormat(AGLPixelFormat pix, GLint attrib, GLint *value);
AGLRendererInfo aglQueryRendererInfo(const AGLDevice *gdevs, GLint ndev);
void aglDestroyRendererInfo(AGLRendererInfo rend);
AGLRendererInfo aglNextRendererInfo(AGLRendererInfo rend);
AGLContext aglGetCurrentContext(void);
GLenum aglGetError(void);
const GLubyte *aglErrorString(GLenum code);
GLboolean aglDescribeRenderer(AGLRendererInfo rend, GLint prop, GLint *value);
AGLContext aglCreateContext(AGLPixelFormat pix, AGLContext share);
GLboolean aglDestroyContext(AGLContext ctx);
GLboolean aglSetCurrentContext(AGLContext ctx);
GLboolean aglSetDrawable(AGLContext ctx, AGLDrawable draw);
GLboolean aglSetFullScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei freq, GLint device);
GLboolean aglUpdateContext(AGLContext ctx);
void aglSwapBuffers(AGLContext ctx);
GLboolean aglEnable(AGLContext ctx, GLenum pname);
GLboolean aglDisable(AGLContext ctx, GLenum pname);
GLboolean aglSetInteger(AGLContext ctx, GLenum pname, const GLint *params);
GLboolean aglGetInteger(AGLContext ctx, GLenum pname, GLint *params);
GLboolean aglIsEnabled(AGLContext ctx, GLenum pname);
AGLPixelFormat aglNextPixelFormat(AGLPixelFormat pix);
AGLDevice *aglDevicesOfPixelFormat(AGLPixelFormat pix, GLint *ndevs);
GLboolean aglCopyContext(AGLContext src, AGLContext dst, GLuint mask);
GLboolean aglSetOffScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei rowbytes, void *baseaddr);
AGLDrawable aglGetDrawable(AGLContext ctx);
GLboolean aglSetVirtualScreen(AGLContext ctx, GLint screen);
GLint aglGetVirtualScreen(AGLContext ctx);
GLboolean aglConfigure(GLenum pname, GLuint param);
GLboolean aglUseFont(AGLContext ctx, GLint fontID, Style face, GLint size, GLint first, GLint count, GLint base);
void aglResetLibrary(void);
/* Apple's current-context data symbol (OpenGLUtility imports it): the GLI
 * object { rend = current AGLContext, dispatch table } (see agl.c) while a
 * context is current, NULL otherwise. */
struct GLIObject;
extern struct GLIObject *glc_cc;
#endif
