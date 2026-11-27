/* DGPacket client layer over the PVMAC ring: the replacement for the
 * transport half of VirtualIntelMac's passthrough-opengl-client.c (MIT,
 * nfzerox, pinned 2861da9e265e557aef90319e4451487ed1fed553): DGPacketNew,
 * DGPacketPointer, DGPacketOutput, DGPacketQueryInput, DGPacketCopy,
 * DGSubmit and DGPacketFree keep their VIM semantics; the byte order handling
 * is new. The PowerPC host is big-endian, the wire is little-endian:
 *   - DGMessage fields and every args[] value are stored little-endian;
 *   - payload elements are swapped per element width on the way in
 *     (DGPacketPointerW) and back on the way out (DGPacketCopyW);
 *   - width 0 (opaque data, e.g. BufferData) is copied verbatim.
 */
#include <MacTypes.h>
#include <Memory.h>
#include <Files.h>
#include <string.h>
#include "dg_client.h"
#include "dg_transport.h"

struct DGContext *dg_current;
struct DGConnection dg_connection;

/* --- byte order */
static inline uint32_t Swap32(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}

static void PutLE32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static uint32_t GetLE32(const unsigned char *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Swap whole elements of `width` bytes in place; a trailing partial element
 * stays as it is. */
static void SwapElements(unsigned char *p, size_t bytes, unsigned width)
{
	size_t i, n;
	unsigned char t;

	if (width < 2)
		return;
	n = bytes / width;
	switch (width) {
	case 2:
		for (i = 0; i < n; i++, p += 2) { t = p[0]; p[0] = p[1]; p[1] = t; }
		break;
	case 4:
		for (i = 0; i < n; i++, p += 4) {
			t = p[0]; p[0] = p[3]; p[3] = t;
			t = p[1]; p[1] = p[2]; p[2] = t;
		}
		break;
	case 8:
		for (i = 0; i < n; i++, p += 8) {
			t = p[0]; p[0] = p[7]; p[7] = t;
			t = p[1]; p[1] = p[6]; p[6] = t;
			t = p[2]; p[2] = p[5]; p[5] = t;
			t = p[3]; p[3] = p[4]; p[4] = t;
		}
		break;
	default:
		break;
	}
}

/* --- packets */
struct DGPacket DGPacketNew(struct DGContext *c, uint32_t op)
{
	struct DGPacket p;

	memset(&p.message, 0, sizeof p.message);
	p.data = NULL;
	p.capacity = 0;
	p.failed = p.valid_reply = p.oom = 0;
	p.message.op = op;
	p.message.context = c ? c->id : 0;
	return p;
}

void DGPacketFree(struct DGPacket *p)
{
	if (p->data && p->data != p->inline_data)
		DisposePtr((Ptr)p->data);
	p->data = NULL;
	p->capacity = 0;
}

/* Make the payload buffer hold `end` bytes, keeping what it already holds. */
static int PacketReserve(struct DGPacket *p, size_t end)
{
	unsigned char *data;

	if (p->data && end <= p->capacity)
		return 1;
	if (!p->data && end <= sizeof p->inline_data) {
		p->data = p->inline_data;
		p->capacity = sizeof p->inline_data;
		return 1;
	}
	data = (unsigned char *)NewPtr(end ? end : 1);
	if (!data) {
		p->failed = p->oom = 1;
		return 0;
	}
	if (p->data) {
		memcpy(data, p->data, p->message.bytes);
		if (p->data != p->inline_data)
			DisposePtr((Ptr)p->data);
	}
	p->data = data;
	p->capacity = end ? end : 1;
	return 1;
}

/* output: 0 copy the caller's bytes in; 1 zero-fill; 2 seed with the caller's
 * bytes (the span is also returned by the executor). */
void DGPacketPointerW(struct DGPacket *p, int index, const void *pointer, size_t bytes, int output, unsigned width)
{
	size_t offset, end;

	if (bytes == SIZE_MAX || (pointer && !bytes) || bytes > DG_MAX_BYTES || p->message.bytes > DG_MAX_BYTES - bytes) {
		p->failed = 1;
		return;
	}
	if (!pointer)
		return;
	offset = ((size_t)p->message.bytes + 7) & ~(size_t)7;
	end = offset + bytes;
	if (end > DG_MAX_BYTES) {
		p->failed = 1;
		return;
	}
	if (!PacketReserve(p, end))
		return;
	if (offset > p->message.bytes)
		memset(p->data + p->message.bytes, 0, offset - p->message.bytes);
	if (bytes) {
		if (output == 1) {
			memset(p->data + offset, 0, bytes);
		} else {
			memcpy(p->data + offset, pointer, bytes);
			SwapElements(p->data + offset, bytes, width);
		}
	}
	p->message.args[index] = offset + 1;
	p->message.bytes = (uint32_t)end;
}

void DGPacketOutputW(struct DGPacket *p, struct DGContext *context, int index, void *pointer, size_t bytes, unsigned width)
{
	if (!bytes) {
		unsigned char unused = 0;

		if (!context || !(context->connection->capabilities & DG_CAP_QUERY_REPLIES)) {
			p->failed = 1;
			return;
		}
		DGPacketPointerW(p, index, &unused, 1, 0, 1);
		return;
	}
	DGPacketPointerW(p, index, pointer, bytes, 2, width);
}

void DGPacketQueryInputW(struct DGPacket *p, int index, const void *pointer, size_t bytes, unsigned width)
{
	if (!bytes) {
		unsigned char unused = 0;

		DGPacketPointerW(p, index, &unused, 1, 0, 1);
		return;
	}
	DGPacketPointerW(p, index, pointer, bytes, 0, width);
}

/* Reply data back to the caller, converted to host element order. */
void DGPacketCopyW(struct DGPacket *p, int index, void *pointer, size_t bytes, unsigned width)
{
	uint64_t offset = p->message.args[index];

	if (p->valid_reply && bytes && pointer && offset && offset - 1 <= p->message.bytes &&
	    bytes <= p->message.bytes - (offset - 1)) {
		memcpy(pointer, p->data + offset - 1, bytes);
		SwapElements((unsigned char *)pointer, bytes, width);
	}
}

/* --- submission */

/* --- compact scalar runs (VIM passthrough-opengl-compact-client.c, MIT, nfzerox)
 * Negotiated (DG_CAP_COMPACT_SCALARS) async zero-body messages are packed as
 * 8-byte DGCompactScalar records + only their leading non-zero args, then sent
 * as one DG_COMPACT_SCALARS container (header context=flags=0, args[0]=1,
 * args[1]=record count >= 2, body <= DG_BATCH_BYTES-144, all little-endian).
 * VIM patches the container in its batch; we gather the run here and emit it
 * whenever anything else reaches the ring, so order is preserved. */
#define COMPACT_MAX (DG_BATCH_BYTES - DG_MESSAGE_BYTES)
static unsigned char gCompact[COMPACT_MAX];
static UInt32 gCompactLen, gCompactCount;
static int gCompactOn = -1;

static void PutLE64(unsigned char *p, uint64_t v)
{
	PutLE32(p, (uint32_t)v);
	PutLE32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t GetLE64(const unsigned char *p)
{
	return (uint64_t)GetLE32(p) | ((uint64_t)GetLE32(p + 4) << 32);
}

static void PutHeader(unsigned char *h, const struct DGMessage *m);

void DGCompactFlush(void)
{
	unsigned char head[DG_MESSAGE_BYTES];
	struct DGMessage m;
	UInt32 count = gCompactCount, len = gCompactLen, words, i;
	const unsigned char *r = gCompact;

	if (!count)
		return;
	gCompactCount = gCompactLen = 0;
	memset(&m, 0, sizeof m);
	if (count == 1) {
		/* A lone record is cheaper (and legal only) as an ordinary message. */
		m.op = r[0] | (r[1] << 8);
		words = r[2] | (r[3] << 8);
		m.context = GetLE32(r + 4);
		for (i = 0; i < words; i++)
			m.args[i] = GetLE64(r + 8 + 8 * i);
		PutHeader(head, &m);
		if (PvmacReserve(DG_MESSAGE_BYTES))
			return;
		PvmacAppend(head, DG_MESSAGE_BYTES);
	} else {
		m.op = DG_COMPACT_SCALARS;
		m.bytes = len;
		m.args[0] = 1;
		m.args[1] = count;
		PutHeader(head, &m);
		if (PvmacReserve(DG_MESSAGE_BYTES + len))
			return;
		PvmacAppend(head, DG_MESSAGE_BYTES);
		PvmacAppend(gCompact, len);
	}
	PvmacMaybeFlush();
}

/* 1 when the message was taken into the run. */
static int CompactAppend(const struct DGMessage *m)
{
	unsigned words, i;
	UInt32 bytes;
	unsigned char *r;

	/* On by default (GLQuake timedemo 105 -> 132 fps); a file named
	 * "OpenGLLibrary.nocompact" in the application's folder turns it off. */
	if (gCompactOn < 0) {
		FSSpec spec;

		gCompactOn = !!FSMakeFSSpec(0, 0, "\pOpenGLLibrary.nocompact", &spec);
		DGLog("compact scalars: %s", gCompactOn ? "on" : "off");
	}
	if (!gCompactOn || !(dg_connection.capabilities & DG_CAP_COMPACT_SCALARS) ||
	    m->op >= 974 || m->flags || m->bytes || !m->context || m->context >= 256)
		return 0;
	words = 16;
	while (words && !m->args[words - 1])
		words--;
	bytes = 8 + 8 * words;
	if (gCompactLen + bytes > COMPACT_MAX)
		DGCompactFlush();
	r = gCompact + gCompactLen;
	r[0] = (unsigned char)m->op; r[1] = (unsigned char)(m->op >> 8);
	r[2] = (unsigned char)words; r[3] = (unsigned char)(words >> 8);
	PutLE32(r + 4, m->context);
	for (i = 0; i < words; i++)
		PutLE64(r + 8 + 8 * i, m->args[i]);
	gCompactLen += bytes;
	gCompactCount++;
	return 1;
}
static void PutHeader(unsigned char *h, const struct DGMessage *m)
{
	int i;

	PutLE32(h, m->op);
	PutLE32(h + 4, m->context);
	PutLE32(h + 8, m->bytes);
	PutLE32(h + 12, m->flags);
	for (i = 0; i < 16; i++) {
		PutLE32(h + 16 + 8 * i, (uint32_t)m->args[i]);
		PutLE32(h + 20 + 8 * i, (uint32_t)(m->args[i] >> 32));
	}
}

static void ConnectionFailed(struct DGContext *context)
{
	dg_connection.dead = 1;
	if (context)
		context->error = GL_INVALID_OPERATION;
}

/* Send p with a reply and read only the 16-byte DGReply (first doorbell).
 * 0 when the reply header arrived; *reply is filled in. A non-zero
 * reply->error is not a transport failure. */
int DGSubmitHeader(struct DGContext *context, struct DGPacket *p, struct DGReply *reply)
{
	unsigned char head[DG_MESSAGE_BYTES];
	const UInt8 *area;
	UInt32 send;

	memset(reply, 0, sizeof *reply);
	p->valid_reply = 0;
	if (p->oom) {
		if (context)
			context->error = GL_OUT_OF_MEMORY;
		return -1;
	}
	if (!context || p->failed || dg_connection.dead) {
		if (context) {
			DGLog("DGSubmit: op %lx dropped (%s)", (unsigned long)p->message.op,
				dg_connection.dead ? "connection dead" : "payload shape rejected");
			context->error = GL_INVALID_OPERATION;
		}
		return -1;
	}
	p->message.flags |= DG_REPLY;
	DGTraceMessage(p);
	DGCompactFlush();
	PutHeader(head, &p->message);
	send = (p->message.flags & DG_OUTPUT_ONLY) ? 0 : p->message.bytes;
	if (PvmacReserve(DG_MESSAGE_BYTES + send)) {
		context->error = GL_OUT_OF_MEMORY;
		return -1;
	}
	PvmacAppend(head, DG_MESSAGE_BYTES);
	if (send)
		PvmacAppend(p->data, send);
	if (PvmacDoorbell(16)) {
		ConnectionFailed(context);
		return -1;
	}
	area = PvmacReplyArea();
	reply->bytes = GetLE32(area);
	reply->error = GetLE32(area + 4);
	reply->result = (uint64_t)GetLE32(area + 8) | ((uint64_t)GetLE32(area + 12) << 32);
	return 0;
}

struct DGReply DGSubmit(struct DGContext *context, struct DGPacket *p, int sync)
{
	struct DGReply reply;
	unsigned char head[DG_MESSAGE_BYTES];
	UInt32 send, expect, n;

	if (sync) {
		if (DGSubmitHeader(context, p, &reply))
			return reply;
		expect = (p->message.flags & DG_INPUT_ONLY) ? 0 : p->message.bytes;
		if (reply.bytes != expect) {
			DGLog("DGSubmit: op %lx reply %lu bytes, expected %lu", (unsigned long)p->message.op,
				(unsigned long)reply.bytes, (unsigned long)expect);
			ConnectionFailed(context);
			return reply;
		}
		if (reply.bytes) {
			n = reply.bytes;
			if (n > gPv.replySize || PvmacDoorbell(n)) {
				ConnectionFailed(context);
				return reply;
			}
			/* An output-only reply into an unallocated payload is dropped. */
			if (!p->data || n > p->capacity)
				return reply;
			if (!(reply.error && (p->message.flags & DG_OUTPUT_ONLY)))
				memcpy(p->data, PvmacReplyArea(), n);
		}
		if (!reply.error)
			p->valid_reply = 1;
		else
			context->error = reply.error;
		return reply;
	}

	memset(&reply, 0, sizeof reply);
	p->valid_reply = 0;
	if (p->oom) {
		if (context)
			context->error = GL_OUT_OF_MEMORY;
		return reply;
	}
	if (!context || p->failed || dg_connection.dead) {
		if (context) {
			DGLog("DGSubmit: op %lx dropped (%s)", (unsigned long)p->message.op,
				dg_connection.dead ? "connection dead" : "payload shape rejected");
			context->error = GL_INVALID_OPERATION;
		}
		return reply;
	}
	DGTraceMessage(p);
	if (CompactAppend(&p->message))
		return reply;
	DGCompactFlush();
	PutHeader(head, &p->message);
	send = (p->message.flags & DG_OUTPUT_ONLY) ? 0 : p->message.bytes;
	if (PvmacReserve(DG_MESSAGE_BYTES + send)) {
		context->error = GL_OUT_OF_MEMORY;
		return reply;
	}
	PvmacAppend(head, DG_MESSAGE_BYTES);
	if (send)
		PvmacAppend(p->data, send);
	PvmacMaybeFlush();
	return reply;
}
