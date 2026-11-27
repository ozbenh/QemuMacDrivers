/* Logging shared by the UTM RAVE Loader and the UTM RAVE engine: lines go to
 * "UTM RAVE Loader.log" in the System Folder, only while a file named
 * "OpenGLLibrary.debug" exists there (checked once per process). */
#ifndef UTMRAVELOG_H
#define UTMRAVELOG_H
#include <MacTypes.h>

/* Kinds for UTMRAVEFirst: what an identifier counts. */
enum {
	kUTMFirstTag,			/* state variable (TQATagInt/Float/Ptr) */
	kUTMFirstMethod,		/* engine or draw method */
	kUTMFirstPixelType,		/* TQAImagePixelType */
	kUTMFirstMisc,			/* anything else; id chosen by the caller */
	kUTMFirstKinds
};

/* 1 when the log is on. */
int UTMRAVELogOn(void);

/* printf-like: %d %u %x (with an l prefix for long), %s, %c, %%. One line. */
void UTMRAVELog(const char *fmt, ...);

/* 1 the first time (kind, id) is seen in this process, else 0; ids above 255
 * share slots modulo 256. Use it to log each unsupported item once. */
int UTMRAVEFirst(int kind, unsigned long id);

/* 1 when a file with this Pascal name exists in the System Folder. */
int UTMRAVESystemFolderHas(ConstStr255Param name);

#endif
