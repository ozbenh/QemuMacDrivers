/* Logging shared by the UTM RAVE Loader and the UTM RAVE engine; see
 * UTMRAVELog.h. The formatter follows OpenGLLibrary/dg_log.c. */
#include <MacTypes.h>
#include <Files.h>
#include <Folders.h>
#include <string.h>
#include <stdarg.h>
#include "UTMRAVELog.h"

#ifndef fnfErr
#define fnfErr (-43)
#endif

static int gChecked, gOn;
static short gVRef;
static long gDirID;
static unsigned char gSeen[kUTMFirstKinds][256 / 8];

static void Put(char **o, char *end, const char *s, size_t n)
{
	while (n-- && *o < end)
		*(*o)++ = *s++;
}

static void PutNumber(char **o, char *end, unsigned long v, unsigned base, int neg)
{
	char t[24];
	int i = 0;

	do {
		t[i++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	if (neg)
		Put(o, end, "-", 1);
	while (i)
		Put(o, end, &t[--i], 1);
}

int UTMRAVESystemFolderHas(ConstStr255Param name)
{
	short vref;
	long dirID;
	FSSpec spec;

	return FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &vref, &dirID) == noErr &&
	       FSMakeFSSpec(vref, dirID, name, &spec) == noErr;
}

int UTMRAVELogOn(void)
{
	FSSpec spec;

	if (!gChecked) {
		gChecked = 1;
		gOn = FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &gVRef, &gDirID) == noErr &&
		      FSMakeFSSpec(gVRef, gDirID, "\pOpenGLLibrary.debug", &spec) == noErr;
	}
	return gOn;
}

void UTMRAVELog(const char *fmt, ...)
{
	char buf[200], *o = buf, *end = buf + sizeof buf - 1;
	va_list ap;
	FSSpec spec;
	short ref;
	long count;

	if (!UTMRAVELogOn())
		return;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		int l = 0;

		if (*fmt != '%') {
			Put(&o, end, fmt, 1);
			continue;
		}
		fmt++;
		if (*fmt == 'l') {
			l = 1;
			fmt++;
		}
		switch (*fmt) {
		case 'd': {
			long v = l ? va_arg(ap, long) : va_arg(ap, int);

			PutNumber(&o, end, v < 0 ? (unsigned long)-v : (unsigned long)v, 10, v < 0);
			break;
		}
		case 'u': PutNumber(&o, end, l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0); break;
		case 'x': PutNumber(&o, end, l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0); break;
		case 's': {
			const char *s = va_arg(ap, const char *);

			Put(&o, end, s ? s : "(null)", s ? strlen(s) : 6);
			break;
		}
		case 'c': { char c = (char)va_arg(ap, int); Put(&o, end, &c, 1); break; }
		case '%': Put(&o, end, "%", 1); break;
		case 0: fmt--; break;
		default: break;
		}
	}
	va_end(ap);
	*o++ = '\r';
	count = o - buf;
	if (FSMakeFSSpec(gVRef, gDirID, "\pUTM RAVE Loader.log", &spec) == fnfErr)
		FSpCreate(&spec, '????', 'TEXT', 0);
	if (FSpOpenDF(&spec, fsRdWrPerm, &ref))
		return;
	SetFPos(ref, fsFromLEOF, 0);
	FSWrite(ref, &count, buf);
	FSClose(ref);
}

int UTMRAVEFirst(int kind, unsigned long id)
{
	unsigned char *b, m;

	if (kind < 0 || kind >= kUTMFirstKinds)
		return 1;
	id &= 255;
	b = &gSeen[kind][id >> 3];
	m = (unsigned char)(1 << (id & 7));
	if (*b & m)
		return 0;
	*b |= m;
	return 1;
}
