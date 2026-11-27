/* Minimal logger: appends text to "OpenGLLibrary.log" in the default folder
 * (the application's folder under the Finder), at most 16 KiB. A shared
 * library has no console, and failures must not be silent.
 *
 * Verbose mode: when a file "OpenGLLibrary.debug" exists in the System Folder
 * (checked once), the log goes to "OpenGLLibrary.log" in the System Folder
 * instead (limit 256 KiB), and DGVLog lines (every agl* call, glGetString,
 * glGetIntegerv) are written too. The first verbose line names the process. */
#include <MacTypes.h>
#include <Files.h>
#include <Processes.h>
#include <string.h>
#include <stdarg.h>
#include "dg_client.h"

/* Multiversal declares FindFolder and kSystemFolderType (Multiverse.h) but
 * not these two. */
#define LOG_ON_SYSTEM_DISK ((short)0x8000)	/* kOnSystemDisk */
#define LOG_DONT_CREATE 0			/* kDontCreateFolder */

#define LOG_LIMIT 16384
#define LOG_LIMIT_VERBOSE 262144
#ifndef fnfErr
#define fnfErr (-43)
#endif

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

static int gVerbose = -1;		/* -1: not checked yet */
static short gSysVRef;
static long gSysDirID;

static void VLog(const char *fmt, va_list ap);

/* Checked once: is OpenGLLibrary.debug in the System Folder? The first call
 * that finds it logs the process this library was loaded into. */
int DGVerbose(void)
{
	FSSpec spec;
	ProcessSerialNumber psn;
	ProcessInfoRec info;
	Str255 name;
	char cname[64];
	int n;

	if (gVerbose >= 0)
		return gVerbose;
	gVerbose = 0;
	if (FindFolder(LOG_ON_SYSTEM_DISK, kSystemFolderType, LOG_DONT_CREATE, &gSysVRef, &gSysDirID) != noErr ||
	    FSMakeFSSpec(gSysVRef, gSysDirID, "\pOpenGLLibrary.debug", &spec) != noErr)
		return 0;
	gVerbose = 1;
	name[0] = 0;
	info.processInfoLength = sizeof info;
	info.processName = name;
	info.processAppSpec = NULL;
	info.processType = info.processSignature = 0;
	psn.highLongOfPSN = 0;
	psn.lowLongOfPSN = kNoProcess;
	if (GetCurrentProcess(&psn) == noErr)
		GetProcessInformation(&psn, &info);
	n = name[0] < sizeof cname - 1 ? name[0] : (int)sizeof cname - 1;
	memcpy(cname, name + 1, n);
	cname[n] = 0;
	DGLog("verbose log on: process \"%s\" type %lx creator %lx", cname,
		(unsigned long)info.processType, (unsigned long)info.processSignature);
	return 1;
}

void DGVLog(const char *fmt, ...)
{
	va_list ap;

	if (!DGVerbose())
		return;
	va_start(ap, fmt);
	VLog(fmt, ap);
	va_end(ap);
}

void DGLog(const char *fmt, ...)
{
	va_list ap;

	DGVerbose();
	va_start(ap, fmt);
	VLog(fmt, ap);
	va_end(ap);
}

static void VLog(const char *fmt, va_list ap)
{
	static int written;
	char buf[384], *o = buf, *end = buf + sizeof buf - 1;
	FSSpec spec;
	short ref;
	long count;

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
	*o++ = '\r';
	count = o - buf;

	if (written + count > (gVerbose > 0 ? LOG_LIMIT_VERBOSE : LOG_LIMIT))
		return;
	if (FSMakeFSSpec(gVerbose > 0 ? gSysVRef : 0, gVerbose > 0 ? gSysDirID : 0, "\pOpenGLLibrary.log", &spec) == fnfErr)
		FSpCreate(&spec, '????', 'TEXT', 0);
	if (FSpOpenDF(&spec, fsRdWrPerm, &ref))
		return;
	SetFPos(ref, fsFromLEOF, 0);
	FSWrite(ref, &count, buf);
	FSClose(ref);
	written += count;
}
