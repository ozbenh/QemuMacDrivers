/* cfrg resource of the OpenGLLibrary replacement (CFM shared library).
 * Recipe from Retro68's Samples/SharedLibrary. The fragment name is what
 * applications import ("OpenGLLibrary"). Versions: current 1.2.0 final
 * (NumVersion 0x01208000), oldest definition 0 (GLQuake 1.1b3 imports with
 * oldImpVersion 0, currentVersion 1); the same values are patched into the
 * PEF container header by CMakeLists.txt. */
#include "Processes.r"
#include "CodeFragments.r"

resource 'cfrg' (0) {
	{
		kPowerPCCFragArch, kIsCompleteCFrag, 0x01208000, 0x00000000,
		kDefaultStackSize, kNoAppSubFolder,
		kImportLibraryCFrag, kDataForkCFragLocator, kZeroOffset, kCFragGoesToEOF,
		"OpenGLLibrary"
	}
};
