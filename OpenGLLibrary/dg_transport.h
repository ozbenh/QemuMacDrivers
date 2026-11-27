/* PVMAC v2 ring transport (mac99 VGA, qemu_vga.ndrv private csCode 0x8300).
 * Registers are 32-bit little-endian at PvmacInfo.regBase. See
 * GLHello/GLHello.c for the bring-up of this protocol. */
#ifndef DG_TRANSPORT_H
#define DG_TRANSPORT_H
#include <MacTypes.h>
#include <Processes.h>
#include <stdint.h>

/* copied from QemuVGADriver/src/QemuVga.h */
#define PVMAC_CS_GET_INFO ((short)0x8300)
typedef struct PvmacInfo {
	UInt32 version, caps, regBase, vramBase;
	UInt32 ringOffset, ringSize, replyOffset, replySize;
} PvmacInfo;
#define PVMAC_CAP_GL_STREAM 2

#define PV_REG_RING_HEAD    0x30
#define PV_REG_RING_TAIL    0x34
#define PV_REG_REPLY_LEN    0x40
#define PV_REG_DOORBELL     0x44
#define PV_REG_GL_STATUS    0x48
#define PV_REG_REPLY_EXPECT 0x4c
#define PV_REG_REPLY_DEST   0x50
#define PV_REG_REPLY_PITCH  0x54
#define PV_REG_REPLY_ROW    0x58
#define PV_REG_REPLY_FLAGS  0x5c
#define PV_FLAG_SWAP32 1
#define PV_FLAG_FLIP   2
#define PV_FLAG_PRESENT 8	/* rows go to REPLY_DEST (offset 0 is valid: full screen at the VRAM base) */
#define PV_FLAG_RGB555 4	/* BGRA8 LE -> big-endian RGB555, dest row = ROW/2 */
#define PV_REG_STREAM_RESET 0x60

extern PvmacInfo gPv;

/* State shared by every process that uses the library (several GL applications
 * at once). Mac OS 9 gives each process its own copy of a CFM library's data,
 * but the device has ONE ring and ONE GL stream, so the ring position and the
 * DG_HELLO result live in a block in the system heap, found through the
 * Gestalt selector 'pvGL'. Processes are scheduled cooperatively: nothing else
 * runs inside a library call, so the block needs no lock, and the reply area
 * (filled and read within one call) can be shared as it is. */
#define PV_GESTALT_SELECTOR 'pvGL'
#define PV_SHARED_MAGIC 0x7076474cUL		/* 'pvGL' */
#define PV_SHARED_VERSION 1
struct PvmacShared {
	UInt32 magic, version;
	UInt32 head, pending;			/* ring write position, bytes not yet handed over */
	UInt32 users;				/* processes attached */
	int ready;				/* 1 once the stream was reset and head/pending read */
	int hello;				/* DG_HELLO: 1 done, -1 failed, 0 not yet */
	uint64_t capabilities;			/* negotiated by that DG_HELLO */
	unsigned features, features2, features3;
	ProcessSerialNumber owner;		/* process that appended the unflushed bytes */
	int ownerValid;
};
/* NULL before PvmacOpen succeeded. */
struct PvmacShared *PvmacSharedBlock(void);
/* Process exit (CFM termination routine): hand over what is queued and leave.
 * The last process out makes the next one start a fresh stream (reset +
 * DG_HELLO), which also frees host contexts of processes that never ran their
 * termination routine. */
void PvmacDetach(void);

/* 0 on success. Idempotent. */
int PvmacOpen(void);
UInt32 PvmacRegRd(UInt32 off);
void PvmacRegWr(UInt32 off, UInt32 v);

/* Make room for one whole message of n bytes (flushes queued bytes to the
 * executor when the ring would overflow); then append pieces with
 * PvmacAppend. A larger message is streamed in ring-sized pieces. */
int PvmacReserve(UInt32 n);
void PvmacAppend(const void *p, UInt32 n);
/* Hand everything appended so far to the executor (asynchronous). */
void PvmacFlush(void);
/* Hand over everything appended and read exactly `expect` reply bytes into
 * the reply area (REPLY_DEST must be 0 unless presenting). 0 on success. */
int PvmacDoorbell(UInt32 expect);
const UInt8 *PvmacReplyArea(void);
/* Flush once enough is queued (call after a message without reply). */
void PvmacMaybeFlush(void);
/* Bytes appended since the last doorbell. */
UInt32 PvmacPending(void);
#endif
