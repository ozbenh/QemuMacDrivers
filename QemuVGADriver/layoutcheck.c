/*
 * Compile-time check that the Video.h parameter records the Display Manager
 * passes to the driver have the classic 68k ("mac68k", 2-byte) layout, as
 * produced by CodeWarrior. Built by "make layoutcheck"; not linked into the
 * driver.
 */
#include <stddef.h>
#include <Video.h>

#define CHECK(expr) _Static_assert(expr, #expr)

/* { UInt16 csMode; UInt32 csData; UInt16 csPage; Ptr csBaseAddr; UInt32 csReserved; } */
CHECK(offsetof(VDSwitchInfoRec, csData) == 2);
CHECK(offsetof(VDSwitchInfoRec, csPage) == 6);
CHECK(offsetof(VDSwitchInfoRec, csBaseAddr) == 8);
CHECK(sizeof(VDSwitchInfoRec) == 16);

/* { UInt16 csMode; long csData; UInt16 csPage; Ptr csBaseAddr; } */
CHECK(offsetof(VDPageInfo, csData) == 2);
CHECK(offsetof(VDPageInfo, csPage) == 6);
CHECK(offsetof(VDPageInfo, csBaseAddr) == 8);

/* ... Fixed csRefreshRate; DepthMode csMaxDepthMode; UInt32 csResolutionFlags; ... */
CHECK(offsetof(VDResolutionInfoRec, csMaxDepthMode) == 20);
CHECK(offsetof(VDResolutionInfoRec, csResolutionFlags) == 22);
