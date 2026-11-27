#ifndef __QEMU_VGA_H__
#define __QEMU_VGA_H__

/* This must be enabled for the MacOS X version of the timer otherwise
 * we don't know if the call failed and don't back off to non-VBL ops
 */
#define USE_DSL_TIMER

/* Pseudo VBL timer duration in ms */
#define TIMER_DURATION	30

/* Enable use of the PCI IRQ as VBL using non-upstream QEMU VGA
 * extensions
 */
#undef USE_PCI_IRQ

/* --- Qemu/Bochs special registers --- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* --- VModes (fixed table, see QemuEdid.c; mode ID = index + 1) */

struct _vMode {
    UInt16 width;
    UInt16 height;
};

#define kQvNoMode                        0xffffffffUL

extern const struct _vMode qvModes[];
extern UInt32 QemuVga_NumModeIDs(void);
extern Boolean QemuVga_ModeAvailable(UInt32 index);
extern UInt32 QemuVga_NextMode(UInt32 prevIndex);
extern UInt32 QemuVga_FindMode(UInt32 width, UInt32 height);

/* --- PVMAC paravirtual extension (QEMU "VGA,mac-accel=on") ---
 * 32-bit little-endian registers at BAR2 + 0x800; see QEMU
 * docs/specs/standard-vga.rst. Absent on stock QEMU: ID reads 0 or ~0.
 */
#define PVMAC_BASE                       0x800
#define PVMAC_REG_ID                     0x00
#define PVMAC_REG_VERSION                0x04
#define PVMAC_REG_CAPS                   0x08
#define PVMAC_REG_STATUS                 0x0c
#define PVMAC_REG_CUR_CTRL               0x10
#define PVMAC_REG_CUR_POS                0x14
#define PVMAC_REG_CUR_HOT                0x18
#define PVMAC_REG_CUR_SIZE               0x1c
#define PVMAC_REG_CUR_DEFINE             0x20
#define PVMAC_REG_CUR_OFFSET             0x24

#define PVMAC_ID_VALUE                   0x50564d43  /* 'PVMC' */
#define PVMAC_CAP_HWCURSOR               0x00000001
#define PVMAC_STATUS_ERROR               0x00000002
#define PVMAC_CURSOR_EDGE                32          /* we always define 32x32 */

/* --- Internal APIs */

extern OSStatus	QemuVga_Init();
extern OSStatus	QemuVga_Exit();

extern OSStatus	QemuVga_Open();
extern OSStatus	QemuVga_Close();

extern void QemuVga_EnableInterrupts(void);
extern void QemuVga_DisableInterrupts(void);

extern UInt16 QemuVga_InitModes(void);

extern OSStatus	QemuVga_SetDepth(UInt32 bpp);

extern OSStatus	QemuVga_SetColorEntry(UInt32 index, RGBColor *color);
extern OSStatus	QemuVga_GetColorEntry(UInt32 index, RGBColor *color);

extern OSStatus QemuVga_GetModePages(UInt32 index, UInt32 depth,
									 UInt32 *pageSize, UInt32 *pageCount);
extern OSStatus QemuVga_GetModeInfo(UInt32 index, UInt32 *width, UInt32 *height);
extern OSStatus QemuVga_SetMode(UInt32 modeIndex, UInt32 depth, UInt32 page);

extern OSStatus QemuVga_Blank(Boolean blank);

extern Boolean QemuVga_ProbePvmac(void);
extern UInt32 *QemuVga_CursorSlot(void);
extern Boolean QemuVga_CursorDefine(UInt32 hotX, UInt32 hotY);
extern void QemuVga_CursorMove(SInt32 x, SInt32 y, Boolean visible);
extern Boolean GraphicsCoreAllocHardwareCursor(void);

#endif
