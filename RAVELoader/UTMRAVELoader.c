/* UTM RAVE Loader: CFM extension that makes a QuickDraw 3D RAVE engine with
 * 3D acceleration active on Mac OS 9 inside the UTM virtual machine, without
 * modifying any Apple file.
 *
 * The QuickDraw 3D RAVE manager scans the Extensions folder in every process
 * for files of type 'shlb' / creator 'tnsl' and loads each with FragPrepare,
 * so this library registers its engine from its CFM initialization routine.
 * In Mac OS X's Classic environment (Gestalt 'bbox' bit 0) the manager loads
 * Classic RAVE itself; then this library does nothing.
 *
 * Otherwise it registers one of two engines:
 *
 *  - "UTM RAVE" (UTMRAVEEngine.c and its modules): our own engine, drawing
 *    through our OpenGLLibrary. Chosen when OpenGLLibrary is ours (it exports
 *    UTMGLTerm; the library is looked up with GetSharedLibrary and each
 *    OpenGL / AGL entry point resolved at run time) and the System Folder does
 *    not contain a file named "UTM RAVE.classic".
 *  - Apple's "Classic RAVE" (the RAVE engine on OpenGL), when the control file
 *    "UTM RAVE.classic" exists (A/B comparison) or OpenGLLibrary is not ours.
 *    It does what the manager does in Classic mode: GetSharedLibrary "Classic
 *    RAVE", FindSymbol rgl_initialize / rgl_terminate, call rgl_initialize
 *    (result ignored), and rgl_terminate + CloseConnection from the
 *    termination routine.
 *
 * Afterwards it makes the registered engine the default engine of the
 * QuickDraw 3D RAVE manager, which scores the enabled engines: every engine
 * that was there before is disabled with QAEngineDisable. The manager
 * functions are resolved at run time (GetSharedLibrary + FindSymbol; the
 * Universal Interfaces ship no stub library for "QuickDraw(TM) 3D
 * Accelerator"), so the loader still loads when the manager is absent.
 *
 * Logging: only when "OpenGLLibrary.debug" exists in the System Folder;
 * lines go to "UTM RAVE Loader.log" there (UTMRAVELog.c). */
/* Built against Apple's Universal Interfaces (see build-ndrv.yml): Retro68's
 * Multiversal headers have no CodeFragments.h or Folders.h. */
#include <MacTypes.h>
#include <Files.h>
#include <Gestalt.h>
#include <Folders.h>
#include <CodeFragments.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <RAVE.h>
#include <RAVESystem.h>
#include <string.h>
#include "UTMRAVEEngine.h"
#include "UTMRAVELog.h"

typedef void (*RGLProc)(void);

static int gDone;			/* the guard: runs once per process */
static int gActive;			/* Classic RAVE loaded */
static CFragConnectionID gConn;
static RGLProc gTerm;
static int gUTMActive;			/* the UTM RAVE engine registered */
static CFragConnectionID gGLConn;	/* our OpenGLLibrary, held while the engine is registered */

static void Log(const char *msg, long err)
{
	UTMRAVELog("%s %ld", msg, err);
}

static void LogText(const char *msg, const char *text)
{
	UTMRAVELog("%s %s", msg, text);
}

/* The RAVE manager's engine functions, found at run time. */
typedef TQAEngine *(*GetFirstProc)(const TQADevice *);
typedef TQAEngine *(*GetNextProc)(const TQADevice *, const TQAEngine *);
typedef TQAError (*GestaltProc)(const TQAEngine *, TQAGestaltSelector, void *);
typedef TQAError (*DisableProc)(long, long);
typedef TQAError (*CheckProc)(const TQAEngine *, const TQADevice *);
typedef TQAError (*RegisterProc)(TQAEngineGetMethod);

static GetFirstProc pFirst;
static GetNextProc pNext;
static GestaltProc pGestalt;
static DisableProc pDisable;
static CheckProc pCheck;		/* optional, diagnostics only */
static RegisterProc pRegister;		/* QARegisterEngine, needed by the UTM RAVE engine only */

#define MAXENG 32
typedef struct {
	long vendor, engine;
} EngID;

static int FindMgr(void)
{
	CFragConnectionID mc;
	Ptr mainAddr, sym;
	Str255 errName;
	CFragSymbolClass cls;
	OSErr e;

	/* "QuickDraw(TM) 3D Accelerator": the TM sign is Mac Roman 0xAA */
	e = GetSharedLibrary("\pQuickDraw\252 3D Accelerator", kPowerPCCFragArch, kReferenceCFrag, &mc, &mainAddr, errName);
	if (e != noErr) {
		Log("GetSharedLibrary QuickDraw 3D Accelerator: err", e);
		return 0;
	}
	if (FindSymbol(mc, "\pQADeviceGetFirstEngine", &sym, &cls) != noErr)
		return 0;
	pFirst = (GetFirstProc)sym;
	if (FindSymbol(mc, "\pQADeviceGetNextEngine", &sym, &cls) != noErr)
		return 0;
	pNext = (GetNextProc)sym;
	if (FindSymbol(mc, "\pQAEngineGestalt", &sym, &cls) != noErr)
		return 0;
	pGestalt = (GestaltProc)sym;
	if (FindSymbol(mc, "\pQAEngineDisable", &sym, &cls) != noErr)
		return 0;
	pDisable = (DisableProc)sym;
	if (FindSymbol(mc, "\pQAEngineCheckDevice", &sym, &cls) == noErr)
		pCheck = (CheckProc)sym;
	if (FindSymbol(mc, "\pQARegisterEngine", &sym, &cls) == noErr)
		pRegister = (RegisterProc)sym;
	return 1;
}

/* Lists the engines for one device (NULL = any) into ids; returns the count. */
static int EnumWith(const TQADevice *dev, EngID *ids, int verbose)
{
	TQAEngine *eng;
	int n = 0;

	for (eng = pFirst(dev); eng && n < MAXENG; eng = pNext(dev, eng)) {
		long vendor = -1, engid = -1, len = 0;
		unsigned long opt = 0, fast = 0;
		char name[256];

		name[0] = 0;
		pGestalt(eng, kQAGestalt_VendorID, &vendor);
		pGestalt(eng, kQAGestalt_EngineID, &engid);
		if (verbose) {
			pGestalt(eng, kQAGestalt_OptionalFeatures, &opt);
			pGestalt(eng, kQAGestalt_FastFeatures, &fast);
			pGestalt(eng, kQAGestalt_ASCIINameLength, &len);
			if (len > 0 && len < 200)
				pGestalt(eng, kQAGestalt_ASCIIName, name);
			Log(" engine vendor", vendor);
			Log("  engine id", engid);
			LogText("  name", name);
			Log("  optional features", (long)opt);
			Log("  fast features", (long)fast);
		}
		ids[n].vendor = vendor;
		ids[n].engine = engid;
		n++;
	}
	return n;
}

/* Engines of any device; if that finds none, those for the main GDevice. */
static int Enum(EngID *ids, int verbose)
{
	TQADevice dev;
	int n = EnumWith(0, ids, verbose);

	if (n == 0) {
		dev.deviceType = kQADeviceGDevice;
		dev.device.gDevice = GetMainDevice();
		n = EnumWith(&dev, ids, verbose);
	}
	return n;
}

static int HasID(const EngID *ids, int n, const EngID *id)
{
	int i;

	for (i = 0; i < n; i++)
		if (ids[i].vendor == id->vendor && ids[i].engine == id->engine)
			return 1;
	return 0;
}

/* Diagnostics: what an application asking for the main screen gets, and
 * whether each engine accepts that device (EngineCheckDevice). */
static void CheckMainDevice(void)
{
	TQADevice dev;
	TQAEngine *eng;

	dev.deviceType = kQADeviceGDevice;
	dev.device.gDevice = GetMainDevice();
	Log("main GDevice", (long)dev.device.gDevice);
	for (eng = pFirst(&dev); eng; eng = pNext(&dev, eng)) {
		long vendor = -1, engid = -1;

		pGestalt(eng, kQAGestalt_VendorID, &vendor);
		pGestalt(eng, kQAGestalt_EngineID, &engid);
		Log(" engine for main GDevice: vendor", vendor);
		Log("  engine id", engid);
		if (pCheck)
			Log("  QAEngineCheckDevice", pCheck(eng, &dev));
	}
	Log("end of engines for main GDevice", 0);
}

/* Disables every engine that existed before ours was registered, so that
 * ours is the manager's default. */
static void DisableEngines(const EngID *ids, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		Log("disabling vendor", ids[i].vendor);
		Log("  engine id", ids[i].engine);
		Log("  QAEngineDisable result", pDisable(ids[i].vendor, ids[i].engine));
	}
}

/* Registers the UTM RAVE engine. Returns 0 when it does not apply (our
 * OpenGLLibrary is missing; the caller then loads Classic RAVE); 1 when it
 * was chosen, whether or not the registration worked (failures are logged,
 * and no other engine is made the default then). Nothing here talks to the
 * paravirtual device: the extension is loaded into every process, and the
 * engine attaches to the device on first real use. */
static int InitUTM(void)
{
	CFragConnectionID lib;
	Ptr mainAddr, sym;
	Str255 errName;
	CFragSymbolClass cls;
	EngID before[MAXENG], after[MAXENG];
	int nBefore, nAfter, i, nNew = 0;
	TQAError qe;
	OSErr e;

	e = GetSharedLibrary("\pOpenGLLibrary", kPowerPCCFragArch, kLoadCFrag, &lib, &mainAddr, errName);
	if (e != noErr) {
		Log("GetSharedLibrary OpenGLLibrary: err", e);
		return 0;
	}
	if (FindSymbol(lib, "\pUTMGLTerm", &sym, &cls) != noErr) {
		Log("OpenGLLibrary is not ours (no UTMGLTerm export)", 0);
		CloseConnection(&lib);
		return 0;
	}
	if (!FindMgr() || !pRegister) {
		Log("UTM RAVE: no RAVE manager or no QARegisterEngine", 0);
		CloseConnection(&lib);
		return 1;
	}
	if (!UTMRAVEEngineInit(lib)) {
		Log("UTM RAVE: OpenGLLibrary entry points missing, engine not registered", 0);
		CloseConnection(&lib);
		return 1;
	}
	Log("engines before UTM RAVE", 0);
	nBefore = Enum(before, 1);
	Log("engine count", nBefore);
	qe = pRegister(UTMRAVEEngineGetMethod);
	Log("QARegisterEngine(UTM RAVE) result", qe);
	if (qe != kQANoErr) {
		UTMRAVEEngineTerm();
		CloseConnection(&lib);
		return 1;
	}
	gGLConn = lib;
	gUTMActive = 1;
	Log("engines after UTM RAVE", 0);
	nAfter = Enum(after, 1);
	Log("engine count", nAfter);
	for (i = 0; i < nAfter; i++)
		if (!HasID(before, nBefore, &after[i]))
			nNew++;
	Log("new engines found", nNew);
	if (nNew == 0) {
		Log("UTM RAVE did not appear among the engines: leaving the others enabled", 0);
		return 1;
	}
	DisableEngines(before, nBefore);
	/* no CheckMainDevice here: it would call our EngineCheckDevice, which attaches to the device */
	return 1;
}

/* Classic RAVE (Apple's engine on OpenGL): the original behaviour. */
static OSErr InitClassic(void)
{
	CFragConnectionID conn;
	Ptr mainAddr, sym;
	Str255 errName;
	CFragSymbolClass cls;
	RGLProc init, term;
	OSErr e;
	EngID before[MAXENG], after[MAXENG];
	int nBefore = 0, nAfter, i, nNew = 0, haveMgr;

	e = GetSharedLibrary("\pClassic RAVE", kPowerPCCFragArch, kLoadCFrag, &conn, &mainAddr, errName);
	if (e != noErr) {
		Log("GetSharedLibrary Classic RAVE: err", e);
		return noErr;
	}
	if (FindSymbol(conn, "\prgl_initialize", &sym, &cls) != noErr) {
		Log("rgl_initialize not found", 0);
		CloseConnection(&conn);
		return noErr;
	}
	init = (RGLProc)sym;
	if (FindSymbol(conn, "\prgl_terminate", &sym, &cls) != noErr) {
		Log("rgl_terminate not found", 0);
		CloseConnection(&conn);
		return noErr;
	}
	term = (RGLProc)sym;
	gConn = conn;
	gTerm = term;
	gActive = 1;
	haveMgr = FindMgr();
	if (haveMgr) {
		Log("engines before rgl_initialize", 0);
		nBefore = Enum(before, 1);
		Log("engine count", nBefore);
	}
	Log("calling rgl_initialize", 0);
	init();
	Log("rgl_initialize returned", 0);
	if (!haveMgr)
		return noErr;
	Log("engines after rgl_initialize", 0);
	nAfter = Enum(after, 1);
	Log("engine count", nAfter);
	for (i = 0; i < nAfter; i++)
		if (!HasID(before, nBefore, &after[i]))
			nNew++;
	Log("Classic RAVE engines found", nNew);
	if (nNew == 0)
		return noErr;
	DisableEngines(before, nBefore);
	CheckMainDevice();
	return noErr;
}

/* CFM initialization routine (named by the PEF loader header: initSection /
 * initOffset, set by set-pef-init-term.py). Always noErr. */
OSErr UTMRAVEInit(const CFragInitBlock *block)
{
	long bbox;

	(void)block;
	if (gDone)
		return noErr;
	gDone = 1;
	if (Gestalt('bbox', &bbox) == noErr && (bbox & 1)) {
		Log("in Classic: manager loads Classic RAVE itself", 0);
		return noErr;
	}
	if (UTMRAVESystemFolderHas("\pUTM RAVE.classic"))
		Log("control file \"UTM RAVE.classic\" present: using Apple's Classic RAVE", 0);
	else if (InitUTM())
		return noErr;
	return InitClassic();
}

/* CFM termination routine (termSection / termOffset). */
void UTMRAVETerm(void)
{
	if (gUTMActive) {
		gUTMActive = 0;
		UTMRAVEEngineTerm();
		CloseConnection(&gGLConn);
		Log("UTM RAVE terminated", 0);
	}
	if (!gActive)
		return;
	gActive = 0;
	Log("calling rgl_terminate", 0);
	gTerm();
	CloseConnection(&gConn);
	Log("terminated", 0);
}
