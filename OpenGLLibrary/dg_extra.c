/* Client-side helpers the generated encoders of the full GL 1.2 export list call.
 *
 * Ported (reduced) from VirtualIntelMac's passthrough-opengl-client.c,
 * passthrough-opengl-arrays-client.c and passthrough-opengl-retained-render-client.c
 * (MIT, nfzerox, pinned 2861da9e265e557aef90319e4451487ed1fed553):
 * DGRecordPixelStore(Float), DGArraySlot, DGClientArrayInteger, DGGetClientPointer,
 * DGInterleavedArrays, DGRegisterRenderBuffer and DGRenderMode.
 * The client attribute stack (DGPBOStack) is ours: it keeps the guest copies of the
 * pixel-store and array state in step with the host's own push/pop.
 * Not supported (the encoder sets GL_INVALID_OPERATION and logs): the imaging
 * subset's pixel transfers and glGetCompressedTexImage.
 */
#include <MacTypes.h>
#include <Memory.h>
#include <string.h>
#include "dg_client.h"

static inline uint32_t Swap32(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}

/* --- pixel store (the guest keeps its own copy to size payloads) */
void DGRecordPixelStore(struct DGContext *c, GLenum key, GLint value)
{
	if (!c || c->common_state_blocked)
		return;
	switch (key) {
	case GL_UNPACK_ROW_BYTES_APPLE: case GL_PACK_ROW_BYTES_APPLE:
		if (value >= 0) c->row_bytes[key == GL_PACK_ROW_BYTES_APPLE] = value;
		break;
	case GL_UNPACK_ALIGNMENT: case GL_PACK_ALIGNMENT:
		if (value == 1 || value == 2 || value == 4 || value == 8) c->alignment[key == GL_PACK_ALIGNMENT] = value;
		break;
	case GL_UNPACK_ROW_LENGTH: case GL_PACK_ROW_LENGTH:
		if (value >= 0) c->row_length[key == GL_PACK_ROW_LENGTH] = value;
		break;
	case GL_UNPACK_SKIP_ROWS: case GL_PACK_SKIP_ROWS:
		if (value >= 0) c->skip_rows[key == GL_PACK_SKIP_ROWS] = value;
		break;
	case GL_UNPACK_SKIP_PIXELS: case GL_PACK_SKIP_PIXELS:
		if (value >= 0) c->skip_pixels[key == GL_PACK_SKIP_PIXELS] = value;
		break;
	case GL_UNPACK_IMAGE_HEIGHT: case GL_PACK_IMAGE_HEIGHT:
		if (value >= 0) c->image_height[key == GL_PACK_IMAGE_HEIGHT] = value;
		break;
	case GL_UNPACK_SKIP_IMAGES: case GL_PACK_SKIP_IMAGES:
		if (value >= 0) c->skip_images[key == GL_PACK_SKIP_IMAGES] = value;
		break;
	default:
		break;
	}
}

void DGRecordPixelStoreFloat(struct DGContext *c, GLenum key, GLfloat value)
{
	if (!c || c->common_state_blocked || !(value >= 0))
		return;
	if (key == GL_UNPACK_ROW_BYTES_APPLE || key == GL_PACK_ROW_BYTES_APPLE)
		return;
	if ((key == GL_UNPACK_ALIGNMENT || key == GL_PACK_ALIGNMENT) &&
	    value != 1 && value != 2 && value != 4 && value != 8)
		return;
	if (value + .5f > 2147483647.0f)
		return;
	DGRecordPixelStore(c, key, (GLint)(value + .5f));
}

/* --- client arrays */
int DGClientArrayInteger(struct DGContext *c, GLenum key, GLint *out)
{
	int slot = DGArraySlot(c, key);

	if (slot < 0)
		return 0;
	*out = c->arrays[slot].enabled;
	return 1;
}

/* Edge flag and color index arrays are not part of this client's draw path. */
void DGAuxArrayPointer(struct DGContext *c, unsigned slot, GLenum type, GLsizei stride, const void *pointer)
{
	(void)slot; (void)type; (void)stride; (void)pointer;
	DGLog("glEdgeFlagPointer/glIndexPointer: not supported");
	if (!c->error)
		c->error = GL_INVALID_OPERATION;
}

void DGGetClientPointer(struct DGContext *c, GLenum pname, void **out)
{
	int slot;

	if (c->common_state_blocked) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return;
	}
	if (!out) {
		if (!c->error)
			c->error = GL_INVALID_VALUE;
		return;
	}
	switch (pname) {
	case GL_SELECTION_BUFFER_POINTER: *out = c->render_buffers[0].pointer; return;
	case GL_FEEDBACK_BUFFER_POINTER: *out = c->render_buffers[1].pointer; return;
	case GL_VERTEX_ARRAY_POINTER: slot = 0; break;
	case GL_NORMAL_ARRAY_POINTER: slot = 1; break;
	case GL_COLOR_ARRAY_POINTER: slot = 2; break;
	case GL_SECONDARY_COLOR_ARRAY_POINTER: slot = 3; break;
	case GL_FOG_COORD_ARRAY_POINTER: slot = 4; break;
	case GL_EDGE_FLAG_ARRAY_POINTER: slot = DG_VERTEX_EDGE_SLOT; break;
	case GL_INDEX_ARRAY_POINTER: slot = DG_VERTEX_INDEX_SLOT; break;
	case GL_TEXTURE_COORD_ARRAY_POINTER: slot = 8 + c->client_texture; break;
	default:
		if (!c->error)
			c->error = GL_INVALID_ENUM;
		return;
	}
	*out = (void *)c->arrays[slot].pointer;
}

/* GL 2.1 table 2.5: sizes in 32-bit words, four UBYTE color channels in one word. */
void DGInterleavedArrays(struct DGContext *c, GLenum format, GLsizei stride, const void *pointer)
{
	static const struct { unsigned char texture, color, normal, vertex, packed_color; } layout[] = {
		{0,0,0,2,0},{0,0,0,3,0},{0,4,0,2,1},{0,4,0,3,1},
		{0,3,0,3,0},{0,0,3,3,0},{0,4,3,3,0},{2,0,0,3,0},
		{4,0,0,4,0},{2,4,0,3,1},{2,3,0,3,0},{2,0,3,3,0},
		{2,4,3,3,0},{4,4,3,4,0},
	};
	unsigned f, t, col, n, v, packed, normal_offset, vertex_offset, tex;
	uintptr_t base;

	if (c->common_state_blocked) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return;
	}
	if (format < GL_V2F || format > GL_T4F_C4F_N3F_V4F) {
		if (!c->error)
			c->error = GL_INVALID_ENUM;
		return;
	}
	if (stride < 0) {
		if (!c->error)
			c->error = GL_INVALID_VALUE;
		return;
	}
	f = format - GL_V2F;
	t = layout[f].texture; col = layout[f].color; n = layout[f].normal;
	v = layout[f].vertex; packed = layout[f].packed_color;
	normal_offset = (t + (packed ? 1 : col)) * sizeof(GLfloat);
	vertex_offset = normal_offset + n * sizeof(GLfloat);
	if (!stride)
		stride = (GLsizei)(vertex_offset + v * sizeof(GLfloat));
	base = (uintptr_t)pointer;
	if (base > UINTPTR_MAX - vertex_offset) {
		if (!c->error)
			c->error = GL_INVALID_VALUE;
		return;
	}
	tex = 8 + c->client_texture;
	c->arrays[DG_VERTEX_EDGE_SLOT].enabled = 0;
	c->arrays[DG_VERTEX_INDEX_SLOT].enabled = 0;
	c->arrays[tex].enabled = !!t;
	c->arrays[2].enabled = !!col;
	c->arrays[1].enabled = !!n;
	c->arrays[0].enabled = 1;
	if (t)
		DGArrayPointer(c, tex, t, GL_FLOAT, stride, pointer, GL_FALSE);
	if (col)
		DGArrayPointer(c, 2, col, packed ? GL_UNSIGNED_BYTE : GL_FLOAT, stride,
			(const void *)(base + t * sizeof(GLfloat)), GL_FALSE);
	if (n)
		DGArrayPointer(c, 1, n, GL_FLOAT, stride, (const void *)(base + normal_offset), GL_FALSE);
	DGArrayPointer(c, 0, v, GL_FLOAT, stride, (const void *)(base + vertex_offset), GL_FALSE);
}

/* --- client attribute stack: guest copies of pixel store and array state.
 * The packet still goes to the host (return 0), which pushes its own copy;
 * a rejected push/pop (return 1) is not sent, so both stacks stay in step. */
#define CLIENT_STACK_DEPTH 16

struct DGClientAttrib {
	struct DGClientAttrib *next;
	GLbitfield mask;
	int alignment[2], row_length[2], skip_rows[2], skip_pixels[2], row_bytes[2];
	int image_height[2], skip_images[2];
	struct DGClientArray arrays[DG_ARRAY_SLOTS];
	int client_texture;
};

int DGPBOStack(struct DGContext *c, GLbitfield mask, int pushing)
{
	struct DGClientAttrib *f;

	if (!c)
		return 0;
	if (pushing) {
		int depth = 0;

		for (f = c->attrib_stack; f; f = f->next)
			depth++;
		if (depth >= CLIENT_STACK_DEPTH) {
			if (!c->error)
				c->error = GL_STACK_OVERFLOW;
			return 1;
		}
		f = (struct DGClientAttrib *)NewPtrClear(sizeof *f);
		if (!f) {
			if (!c->error)
				c->error = GL_OUT_OF_MEMORY;
			return 1;
		}
		f->mask = mask;
		memcpy(f->alignment, c->alignment, sizeof f->alignment);
		memcpy(f->row_length, c->row_length, sizeof f->row_length);
		memcpy(f->skip_rows, c->skip_rows, sizeof f->skip_rows);
		memcpy(f->skip_pixels, c->skip_pixels, sizeof f->skip_pixels);
		memcpy(f->row_bytes, c->row_bytes, sizeof f->row_bytes);
		memcpy(f->image_height, c->image_height, sizeof f->image_height);
		memcpy(f->skip_images, c->skip_images, sizeof f->skip_images);
		memcpy(f->arrays, c->arrays, sizeof f->arrays);
		f->client_texture = c->client_texture;
		f->next = c->attrib_stack;
		c->attrib_stack = f;
		return 0;
	}
	f = c->attrib_stack;
	if (!f) {
		if (!c->error)
			c->error = GL_STACK_UNDERFLOW;
		return 1;
	}
	c->attrib_stack = f->next;
	if (f->mask & GL_CLIENT_PIXEL_STORE_BIT) {
		memcpy(c->alignment, f->alignment, sizeof f->alignment);
		memcpy(c->row_length, f->row_length, sizeof f->row_length);
		memcpy(c->skip_rows, f->skip_rows, sizeof f->skip_rows);
		memcpy(c->skip_pixels, f->skip_pixels, sizeof f->skip_pixels);
		memcpy(c->row_bytes, f->row_bytes, sizeof f->row_bytes);
		memcpy(c->image_height, f->image_height, sizeof f->image_height);
		memcpy(c->skip_images, f->skip_images, sizeof f->skip_images);
	}
	if (f->mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
		memcpy(c->arrays, f->arrays, sizeof f->arrays);
		c->client_texture = f->client_texture;
	}
	DisposePtr((Ptr)f);
	return 0;
}

/* Free the stack when the context goes away. */
void DGClientAttribFree(struct DGContext *c)
{
	while (c->attrib_stack) {
		struct DGClientAttrib *f = c->attrib_stack;

		c->attrib_stack = f->next;
		DisposePtr((Ptr)f);
	}
}

/* --- selection and feedback: the host keeps the result, the guest buffer is
 * filled at glRenderMode. Words and the status record arrive little-endian. */
static int RenderBufferIndex(uint32_t mode)
{
	return mode == GL_SELECT ? 0 : mode == GL_FEEDBACK ? 1 : -1;
}

void DGRegisterRenderBuffer(struct DGContext *c, unsigned op, int which, GLsizei count, GLenum type, void *pointer)
{
	GLenum error = GL_NO_ERROR;
	struct DGPacket p;

	if (c->common_state_blocked || !(c->connection->features2 & DG_FEATURE2_RETAINED_RENDER))
		error = GL_INVALID_OPERATION;
	else if (count < 0 || (count && !pointer))
		error = GL_INVALID_VALUE;
	else if ((uint32_t)count > DG_MAX_BYTES / 4)
		error = GL_OUT_OF_MEMORY;
	if (error) {
		if (!c->error)
			c->error = error;
		return;
	}
	p = DGPacketNew(c, op);
	p.message.args[0] = (uint32_t)count;
	p.message.args[1] = type;
	DGSubmit(c, &p, 1);
	if (p.valid_reply) {
		c->render_buffers[which].pointer = pointer;
		c->render_buffers[which].words = (uint32_t)count;
	}
	DGPacketFree(&p);
}

GLint DGRenderMode(struct DGContext *c, unsigned op, GLenum mode)
{
	struct {
		uint32_t mode, error;
		uint32_t words[DG_RENDER_INLINE_WORDS];
	} result;
	struct DGPacket p;
	struct DGReply r;
	int previous;
	unsigned capacity, inline_words, written, inline_written, offset, i;
	unsigned char *out;
	uint32_t status_error;

	if (c->common_state_blocked) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return 0;
	}
	p = DGPacketNew(c, op);
	p.message.args[0] = mode;
	if (!(c->connection->features2 & DG_FEATURE2_RETAINED_RENDER)) {
		r = DGSubmit(c, &p, 1);
		DGPacketFree(&p);
		return (GLint)(uint32_t)r.result;
	}
	previous = RenderBufferIndex(c->render_mode);
	capacity = previous < 0 ? 0 : c->render_buffers[previous].words;
	inline_words = capacity < DG_RENDER_INLINE_WORDS ? capacity : DG_RENDER_INLINE_WORDS;
	p.message.args[1] = 1;
	p.message.flags = DG_OUTPUT_ONLY;
	p.data = (unsigned char *)&result;		/* borrowed: never DGPacketFree */
	p.message.bytes = (uint32_t)(p.capacity = 8 + inline_words * 4);
	r = DGSubmit(c, &p, 1);
	if (!p.valid_reply)
		return 0;
	c->render_mode = Swap32(result.mode);
	status_error = Swap32(result.error);
	written = (uint32_t)(r.result >> 32);
	if (written > capacity) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return 0;
	}
	if (written) {
		out = (unsigned char *)c->render_buffers[previous].pointer;
		inline_written = written < inline_words ? written : inline_words;
		memcpy(out, result.words, inline_written * 4);
		for (offset = inline_written; offset < written;) {
			struct DGPacket chunk;
			unsigned count = written - offset;

			if (count > DG_RENDER_CHUNK_WORDS)
				count = DG_RENDER_CHUNK_WORDS;
			chunk = DGPacketNew(c, DG_RETAINED_RENDER_READ);
			chunk.message.args[0] = offset;
			chunk.message.flags = DG_OUTPUT_ONLY;
			chunk.data = out + offset * 4;		/* borrowed */
			chunk.message.bytes = (uint32_t)(chunk.capacity = count * 4);
			DGSubmit(c, &chunk, 1);
			if (!chunk.valid_reply)
				return 0;
			offset += count;
		}
		for (i = 0; i < written; i++)		/* wire words -> PowerPC order */
			((uint32_t *)out)[i] = Swap32(((uint32_t *)out)[i]);
	}
	if (status_error && !c->error)
		c->error = status_error;
	return (GLint)(uint32_t)r.result;
}

/* --- evaluator control points (glMap1/2): DGPackEvaluatorPoints of VIM's
 * passthrough-opengl-client.c with the payload swapped per element. The host
 * first validates the call without data (args[15]); only then the tightly
 * packed control points, never the stride padding, go into the packet. */
int DGPackEvaluatorPointsW(struct DGContext *c, struct DGPacket *packet, int index, const void *points,
	unsigned width, GLint ustride, GLint uorder, GLint vstride, GLint vorder, size_t element, int dimensions,
	unsigned stride_arg, unsigned elemwidth)
{
	struct DGPacket check;
	struct DGReply reply;
	size_t bytes, point_bytes, i, j, k;
	uint64_t span;
	unsigned char *dst;
	const unsigned char *src = (const unsigned char *)points;

	if (!(c->connection->features & DG_FEATURE_EVALUATOR_UPLOADS)) {
		if (!c->error)
			c->error = GL_INVALID_OPERATION;
		return 0;
	}
	memset(&check, 0, sizeof check);
	check.message = packet->message;
	check.message.args[15] = 1;
	reply = DGSubmit(c, &check, 1);
	DGPacketFree(&check);
	if (!check.valid_reply || reply.result != 1)
		return 0;
	bytes = DGEvaluatorCoefficientBytes(width, uorder, vorder, element);
	span = (uint64_t)(uorder - 1) * ustride + (uint64_t)(vorder - 1) * vstride + width;
	if (!points || !width || bytes == SIZE_MAX || span > (UINTPTR_MAX - (uintptr_t)points) / element) {
		if (!c->error)
			c->error = GL_INVALID_VALUE;
		return 0;
	}
	dst = (unsigned char *)NewPtr(bytes ? bytes : 1);
	if (!dst) {
		if (!c->error)
			c->error = GL_OUT_OF_MEMORY;
		return 0;
	}
	DGPacketFree(packet);			/* nothing in it yet; keep the message */
	packet->data = dst;
	packet->capacity = bytes ? bytes : 1;
	point_bytes = width * element;
	for (i = 0; i < (size_t)uorder; ++i) {
		for (j = 0; j < (size_t)vorder; ++j) {
			size_t offset = ((size_t)i * ustride + (size_t)j * vstride) * element;

			memcpy(dst, src + offset, point_bytes);
			if (elemwidth == 4) {
				for (k = 0; k + 4 <= point_bytes; k += 4)
					((uint32_t *)(dst + k))[0] = Swap32(((uint32_t *)(dst + k))[0]);
			} else if (elemwidth == 8) {
				for (k = 0; k + 8 <= point_bytes; k += 8) {
					uint32_t a = Swap32(((uint32_t *)(dst + k))[0]);

					((uint32_t *)(dst + k))[0] = Swap32(((uint32_t *)(dst + k))[1]);
					((uint32_t *)(dst + k))[1] = a;
				}
			}
			dst += point_bytes;
		}
	}
	packet->message.args[stride_arg] = (uint64_t)vorder * width;
	if (dimensions == 2)
		packet->message.args[stride_arg + 4] = width;
	packet->message.args[index] = 1;
	packet->message.bytes = (uint32_t)bytes;
	return 1;
}

/* --- not supported */
void DGReadTexture(struct DGContext *c, GLenum target, GLint level, GLenum format, GLenum type, void *pixels,
	int compressed, GLint row_bytes)
{
	(void)target; (void)level; (void)format; (void)type; (void)pixels; (void)compressed; (void)row_bytes;
	if (!c)
		return;
	DGLog("glGetCompressedTexImage: not supported");
	if (!c->error)
		c->error = GL_INVALID_OPERATION;
}

void DGImagingClientTransfer(struct DGContext *c, struct DGPacket *p, enum DGImagingTransfer op, GLenum target,
	GLint width, GLint height, GLint start, GLenum format, GLenum type, int pack, const int indices[2],
	const void *const pixels[2])
{
	(void)p; (void)op; (void)target; (void)width; (void)height; (void)start; (void)format; (void)type;
	(void)pack; (void)indices; (void)pixels;
	DGLog("imaging subset pixel transfer: not supported");
	if (!c->error)
		c->error = GL_INVALID_OPERATION;
}
