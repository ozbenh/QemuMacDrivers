/* Exports that VirtualIntelMac's generator leaves to a hand-written client
 * ("custom-client" slots in opengl-codegen-ppc.json). Only the ones a listed
 * application imports are written: glGetString (GLQuake), glGetError (Quake III). */
#include "dg_client.h"

/* Static answers, not a round trip to the host: Mac OS 9 applications only
 * need to know the GL 1.2 level and the two multitexture extensions below,
 * both of which the host executor runs natively (its GL 2.1 context has
 * ARB multitexture and the ADD texture environment). Never list more than
 * the executor supports. */
static const char kVendor[] = "UTM";
static const char kRenderer[] = "UTM PassthroughOpenGL";
static const char kVersion[] = "1.2";
static const char kExtensions[] = "GL_ARB_multitexture GL_EXT_texture_env_add";

static const GLubyte *GetStringImpl(GLenum name)
{
	if (!dg_current)
		return NULL;
	switch (name) {
	case 0x1F00: return (const GLubyte *)kVendor;		/* GL_VENDOR */
	case 0x1F01: return (const GLubyte *)kRenderer;		/* GL_RENDERER */
	case 0x1F02: return (const GLubyte *)kVersion;		/* GL_VERSION */
	case 0x1F03: return (const GLubyte *)kExtensions;	/* GL_EXTENSIONS */
	default:
		dg_current->error = GL_INVALID_ENUM;
		return NULL;
	}
}

const GLubyte *glGetString(GLenum name)
{
	const GLubyte *r = GetStringImpl(name);

	DGVLog("glGetString(%lx) -> \"%s\"", (unsigned long)name, (const char *)r);
	return r;
}

/* Errors raised in the client (validation, arrays) or by AGL; the host's own
 * GL errors are not read back. */
GLenum glGetError(void)
{
	GLenum e;

	if (!dg_current)
		return GL_NO_ERROR;
	e = dg_current->error;
	dg_current->error = GL_NO_ERROR;
	return e;
}
