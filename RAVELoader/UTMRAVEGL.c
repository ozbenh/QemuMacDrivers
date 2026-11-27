/* Run-time resolution of the OpenGLLibrary entry points; see UTMRAVEGL.h. */
#include <MacTypes.h>
#include <CodeFragments.h>
#include <string.h>
#include "UTMRAVEGL.h"
#include "UTMRAVELog.h"

struct UTMGL gGL;

/* FindSymbol takes a Pascal string. */
static int Find(CFragConnectionID lib, const char *name, void **out)
{
	Str255 pname;
	Ptr sym;
	CFragSymbolClass cls;
	size_t n = strlen(name);

	pname[0] = (unsigned char)n;
	memcpy(pname + 1, name, n);
	if (FindSymbol(lib, pname, &sym, &cls) != noErr || !sym || cls != kTVectorCFragSymbol) {
		UTMRAVELog("OpenGLLibrary export missing: %s", name);
		return 0;
	}
	*out = sym;
	return 1;
}

int UTMRAVEGLResolve(CFragConnectionID lib)
{
	int ok = 1;

#define UTM_GL_FIND(ret, name, args) \
	if (!Find(lib, #name, (void **)&gGL.name)) \
		ok = 0;
	UTM_GL_FUNCTIONS(UTM_GL_FIND)
#undef UTM_GL_FIND
	return ok;
}
