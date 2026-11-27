/* cfrg resource of "UTM RAVE Loader" (CFM shared library, PowerPC). Recipe as
 * OpenGLLibrary.r. The fragment is never imported by name: the QuickDraw 3D
 * RAVE manager loads every 'shlb'/'tnsl' file of the Extensions folder with
 * FragPrepare, which runs the CFM initialization routine. */
#include "Processes.r"
#include "CodeFragments.r"

resource 'cfrg' (0) {
	{
		kPowerPCCFragArch, kIsCompleteCFrag, 0x00000000, 0x00000000,
		kDefaultStackSize, kNoAppSubFolder,
		kImportLibraryCFrag, kDataForkCFragLocator, kZeroOffset, kCFragGoesToEOF,
		"UTM RAVE Loader"
	}
};
