/* Client vertex arrays for the OS 9 OpenGLLibrary.
 *
 * Ported (reduced) from VirtualIntelMac's passthrough-opengl-arrays-client.c
 * (MIT, nfzerox, pinned 2861da9e265e557aef90319e4451487ed1fed553):
 * DGArrayPointer, DGEnableClientArray, DGClientTexture, DGDraw (DGDrawInstancesBase
 * without instancing/base vertex/VBO/draw lists/coalescing) and DGArrayElement.
 *
 * The guest only tracks the array pointers. At a draw call the referenced
 * vertex ranges and the indices are copied into one DG_DRAW message. The
 * data stays in PowerPC (big-endian) order: args[13] = 1 tells the host to
 * convert the interpreted components (VIM's DG_CAP_BIG_ENDIAN_ARRAYS path,
 * made for Rosetta guests); only the descriptors and the message header are
 * little-endian. Without that host capability a draw is dropped and logged. */
#include <string.h>
#include "dg_client.h"

static unsigned VertexBytes(GLenum type)
{
	switch (type) {
	case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
	case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
	case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
	case GL_DOUBLE: return 8;
	default: return 0;
	}
}

static int ArraySlot(struct DGContext *c, GLenum array)
{
	switch (array) {
	case GL_VERTEX_ARRAY: return 0;
	case GL_NORMAL_ARRAY: return 1;
	case GL_COLOR_ARRAY: return 2;
	case GL_SECONDARY_COLOR_ARRAY: return 3;
	case GL_FOG_COORDINATE_ARRAY: return 4;
	case GL_TEXTURE_COORD_ARRAY: return 8 + c->client_texture;
	default: return -1;
	}
}

int DGArraySlot(struct DGContext *c, GLenum array)
{
	return ArraySlot(c, array);
}

void DGClientTexture(struct DGContext *c, GLenum unit)
{
	if (!c)
		return;
	if (unit < GL_TEXTURE0 || unit >= GL_TEXTURE0 + 8) {
		c->error = GL_INVALID_ENUM;
		return;
	}
	c->client_texture = (int)(unit - GL_TEXTURE0);
}

void DGArrayPointer(struct DGContext *c, unsigned slot, GLint size, GLenum type, GLsizei stride,
	const void *pointer, GLboolean normalized)
{
	struct DGClientArray *a;

	(void)normalized;
	if (!c)
		return;
	if (slot >= DG_ARRAY_SLOTS || size < 1 || size > 4 || stride < 0 || !VertexBytes(type)) {
		c->error = GL_INVALID_VALUE;
		return;
	}
	a = &c->arrays[slot];
	a->size = size;
	a->type = type;
	a->stride = stride;
	a->pointer = pointer;
}

void DGEnableClientArray(struct DGContext *c, GLenum array, int enabled)
{
	int slot;

	if (!c)
		return;
	slot = ArraySlot(c, array);
	if (slot < 0) {
		c->error = GL_INVALID_ENUM;
		return;
	}
	c->arrays[slot].enabled = enabled;
}

/* --- little-endian wire records */
static void PutLE32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void PutLE64(unsigned char *p, uint64_t v)
{
	PutLE32(p, (uint32_t)v);
	PutLE32(p + 4, (uint32_t)(v >> 32));
}

/* struct DGVertexArray (40 bytes): slot size type stride normalized buffer pointer bytes */
#define DESC_BYTES 40u

static int BigEndianOK(struct DGContext *c)
{
	if (c->connection->capabilities & DG_CAP_BIG_ENDIAN_ARRAYS)
		return 1;
	DGLog("client arrays: host lacks DG_CAP_BIG_ENDIAN_ARRAYS, draw dropped");
	if (!c->error)
		c->error = GL_INVALID_OPERATION;
	return 0;
}

static size_t ArraySpan(const struct DGClientArray *a, uint64_t count)
{
	size_t element = VertexBytes(a->type) * (size_t)a->size;
	uint64_t stride = a->stride ? (uint64_t)a->stride : element;
	uint64_t bytes;

	if (!count)
		return 0;
	if (count - 1 > DG_MAX_BYTES / stride)
		return SIZE_MAX;
	bytes = (count - 1) * stride + element;
	return bytes > DG_MAX_BYTES ? SIZE_MAX : (size_t)bytes;
}

/* Highest index + 1 of a native (big-endian) index array. */
static uint64_t MaxIndex(const void *indices, GLsizei count, unsigned size)
{
	const unsigned char *p = indices;
	uint64_t high = 0;
	GLsizei i;

	for (i = 0; i < count; ++i, p += size) {
		uint32_t v = 0;
		unsigned j;

		for (j = 0; j < size; ++j)
			v = (v << 8) | p[j];
		if ((uint64_t)v + 1 > high)
			high = (uint64_t)v + 1;
	}
	return high;
}

void DGDraw(struct DGContext *c, GLenum mode, GLint first, GLsizei count, GLenum type, const void *indices)
{
	unsigned char desc[DG_ARRAY_SLOTS * DESC_BYTES];
	size_t spans[DG_ARRAY_SLOTS];
	unsigned slots[DG_ARRAY_SLOTS];
	unsigned n = 0, i;
	uint64_t vertices;
	size_t index_bytes = 0;
	struct DGPacket p;
	uint64_t desc_offset;

	if (!c)
		return;
	if (first < 0 || count < 0 ||
	    (type && type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)) {
		c->error = GL_INVALID_VALUE;
		return;
	}
	if (!count)
		return;
	if (type) {
		if (!indices) {
			c->error = GL_INVALID_VALUE;
			return;
		}
		index_bytes = DGArrayBytes(count, VertexBytes(type));
		if (index_bytes == SIZE_MAX) {
			c->error = GL_INVALID_VALUE;
			return;
		}
		vertices = MaxIndex(indices, count, VertexBytes(type));
	} else {
		vertices = (uint64_t)first + (uint32_t)count;
	}
	if (!BigEndianOK(c))
		return;
	for (i = 0; i < DG_ARRAY_SLOTS; ++i) {
		struct DGClientArray *a = &c->arrays[i];

		if (!a->enabled)
			continue;
		spans[i] = ArraySpan(a, vertices);
		if (!a->pointer || spans[i] == SIZE_MAX) {
			DGLog("client arrays: slot %u unusable (pointer %lx span %lu)", i,
				(unsigned long)a->pointer, (unsigned long)spans[i]);
			if (!c->error)
				c->error = GL_INVALID_OPERATION;
			return;
		}
		slots[n++] = i;
	}
	memset(desc, 0, sizeof desc);
	p = DGPacketNew(c, DG_DRAW);
	p.message.args[0] = mode;
	p.message.args[1] = (uint32_t)first;
	p.message.args[2] = (uint32_t)count;
	p.message.args[3] = type;
	p.message.args[8] = n;
	p.message.args[13] = 1;				/* arrays and indices are PowerPC byte order */
	if (n)
		DGPacketPointerW(&p, 6, desc, n * DESC_BYTES, 0, 1);
	desc_offset = p.message.args[6];
	if (type)
		DGPacketPointerW(&p, 5, indices, index_bytes, 0, 1);
	for (i = 0; i < n && !p.failed; ++i) {
		struct DGClientArray *a = &c->arrays[slots[i]];
		unsigned char *d = desc + i * DESC_BYTES;

		DGPacketPointerW(&p, 9, a->pointer, spans[slots[i]], 0, 1);
		PutLE32(d, slots[i]);
		PutLE32(d + 4, (uint32_t)a->size);
		PutLE32(d + 8, a->type);
		PutLE32(d + 12, (uint32_t)a->stride);
		PutLE64(d + 24, p.message.args[9]);
		PutLE64(d + 32, spans[slots[i]]);
	}
	if (n && !p.failed)
		memcpy(p.data + desc_offset - 1, desc, n * DESC_BYTES);
	if (p.failed) {
		DGLog("client arrays: draw packet failed (n %u count %ld)", n, (long)count);
		if (!c->error)
			c->error = GL_OUT_OF_MEMORY;
	}
	DGSubmit(c, &p, 0);
	DGPacketFree(&p);
}

/* struct DGElementValue (48 bytes): slot size type normalized, 32 data bytes. */
#define VALUE_BYTES 48u

void DGArrayElement(struct DGContext *c, unsigned op, GLint index)
{
	unsigned char values[DG_ARRAY_SLOTS * VALUE_BYTES];
	unsigned count = 0, i;
	struct DGPacket p;

	if (!c)
		return;
	if (!(c->connection->features2 & DG_FEATURE2_ARRAY_ELEMENT)) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return;
	}
	if (index < 0) {
		if (!c->error)
			c->error = GL_INVALID_VALUE;
		return;
	}
	if (!BigEndianOK(c))
		return;
	memset(values, 0, sizeof values);
	for (i = 0; i < DG_ARRAY_SLOTS; ++i) {
		const struct DGClientArray *a = &c->arrays[i];
		size_t bytes;
		uint64_t offset;
		unsigned char *v = values + count * VALUE_BYTES;

		if (!a->enabled)
			continue;
		bytes = VertexBytes(a->type) * (size_t)a->size;
		offset = (uint64_t)index * (a->stride ? (uint64_t)a->stride : bytes);
		if (!bytes || bytes > 32 || !a->pointer) {
			if (!c->error)
				c->error = GL_INVALID_OPERATION;
			return;
		}
		PutLE32(v, i);
		PutLE32(v + 4, (uint32_t)a->size);
		PutLE32(v + 8, a->type);
		memcpy(v + 16, (const char *)a->pointer + (size_t)offset, bytes);
		++count;
	}
	if (!count)
		return;
	p = DGPacketNew(c, op);
	p.message.args[0] = (uint32_t)index;
	p.message.args[1] = count;
	p.message.args[4] = 1;				/* PowerPC byte order */
	DGPacketPointerW(&p, 2, values, count * VALUE_BYTES, 0, 1);
	DGSubmit(c, &p, 0);
	DGPacketFree(&p);
}
