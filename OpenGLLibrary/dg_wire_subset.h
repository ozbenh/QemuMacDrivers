/* Helpers copied unmodified from VirtualIntelMac's passthrough-opengl-wire.h
 * (MIT, https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon, pinned commit
 * 2861da9e265e557aef90319e4451487ed1fed553): array/state/pixel size
 * computation that decides how many payload bytes a GL call carries. They
 * include server-state-query-counts.inc, additional-state-query-counts.inc
 * (VIM sources) and the generated direct-get-counts.inc, and need the GL_
 * names from glenum-values.h.
 * The repository's MIT license applies to this file. */
#ifndef DG_WIRE_SUBSET_H
#define DG_WIRE_SUBSET_H
static size_t DGArrayBytes(int64_t count,size_t size)
{ return count<0 || (uint64_t)count>DG_MAX_BYTES/size ? SIZE_MAX : (size_t)count*size; }
static int DGCompressedProxy(uint32_t target)
{return target==0x8063u || target==0x8064u || target==0x8070u || target==0x851bu ||
    target==0x84f7u || target==0x8c19u || target==0x8c1bu || target==0x900bu;}
/* Native query/generator validation owns nonpositive capacity semantics.
 * These calls carry no array bytes until capacity is positive. Negative
 * and zero-capacity shapes are negotiated; they must not look like malformed
 * wire pointers to an older executor that rejects zero-length spans. */
static size_t DGQueryArrayBytes(int64_t count,size_t size)
{return count<=0?0:DGArrayBytes(count,size);}
/* Unknown Get pnames must not be guessed scalar: that would overwrite a
 * caller's allocation for vector results. Expand from audited API contracts. */
static size_t DGPatchLevelCount(uint32_t key)
{
    return key==0x8e73 ? 2 : key==0x8e74 ? 4 : 0;
}
static size_t DGAdditionalStateCount(uint32_t key)
{
    switch(key){
#define DG_ADDITIONAL_QUERY(name,count) case name:return count;
#include "additional-state-query-counts.inc"
#undef DG_ADDITIONAL_QUERY
    default:return 0;
    }
}
static size_t DGGetCount(uint32_t key)
{
    size_t additional=DGAdditionalStateCount(key);if(additional)return additional;
    if(key==0x1700 || key==0x850a || (key>=0x8722 && key<=0x873f))return 16; /* MODELVIEWn_ARB */
    switch(key) {
#define DG_SERVER_QUERY(name,count) case name:return count;
#include "server-state-query-counts.inc"
#undef DG_SERVER_QUERY
    case 0x0ba6:case 0x0ba7:case 0x0ba8:case 0x0b89:
    case 0x8641:case 0x88b7:return 16; /* matrices, ARB current/transpose */
    case 0x0ba2:case 0x0c10:case 0x0c22:case 0x0c23:case 0x0b00:
    case 0x0b03:case 0x0b04:case 0x0b53:case 0x0b66:
    case GL_CURRENT_RASTER_TEXTURE_COORDS:case GL_CURRENT_RASTER_POSITION:return 4;
    case 0x0b70:case 0x0d3a:case 0x0b12:case 0x0b22:case 0x846d:case 0x846e:case 0x825d:return 2;
    case 0x8e73:case 0x8e74:return DGPatchLevelCount(key);
    case 0x0ba0:case 0x0ba1:case 0x0be2:case 0x0b71:case 0x0b90:case 0x0b44:
    case 0x0c00:case 0x0c01:case 0x0c02:case 0x0c11:case 0x0d05:
    case 0x0cf5:case 0x0cf2:case 0x0cf3:case 0x0cf4:case 0x0d02:case 0x0d03:case 0x0d04:
    case 0x8f9d:case 0x8f9e: /* PRIMITIVE_RESTART, PRIMITIVE_RESTART_INDEX */
    case 0x8e72:case 0x8e7d:case 0x8e7e:case 0x8e7f:case 0x8e80:case 0x8e81:case 0x8e82:
    case 0x8e83:case 0x8e84:case 0x8e85:case 0x8e86:case 0x8e89:case 0x8e8a:
    case 0x886c:case 0x886d:case 0x8e1e:case 0x8e1f: /* patch vertices and tessellation limits */
    case 0x8e51:case 0x8e59: /* SAMPLE_MASK, MAX_SAMPLE_MASK_WORDS */
    case 0x9104:case 0x9105: /* multisample texture bindings */
    case 0x910e:case 0x910f:case 0x9110: /* color/depth/integer texture sample limits */
    case 0x88fc: /* MAX_DUAL_SOURCE_DRAW_BUFFERS */
    case 0x825a: /* PROGRAM_PIPELINE_BINDING */
    case 0x825b:case 0x825c:case 0x825e:case 0x825f: /* viewport limits and provoking vertices */
    case 0x8904:case 0x8e5e:case 0x8e5b: /* scalar minimum texel/gather/interpolation offsets */
    case 0x85b2:case 0x8a15:case 0x8a16:
    case 0x0d33:case 0x8069:case 0x8068:case 0x806a:case 0x84f6:case 0x8514:
    case 0x84e0:case 0x84e1:case 0x84e2:case 0x84f8:case 0x8872:case 0x8b4d:case 0x8b8d:
    case 0x8894:case 0x8895:case 0x88ed:case 0x88ef:
    case 0x8c89:case 0x8c8f: /* RASTERIZER_DISCARD, TRANSFORM_FEEDBACK_BUFFER_BINDING */
    case 0x8e23:case 0x8e24: /* transform-feedback paused, active */
    case 0x8ca6:case 0x8caa:case 0x8ca7:case 0x0d56:case 0x0d57:return 1;
    case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:return 1;
    case GL_SUBPIXEL_BITS:case GL_CURRENT_RASTER_POSITION_VALID:
    case GL_CURRENT_RASTER_INDEX:case GL_CURRENT_RASTER_DISTANCE:return 1;
    case 0x8825:case 0x8826:case 0x8827:case 0x8828:case 0x8829:case 0x882a:case 0x882b:case 0x882c:
    case 0x882d:case 0x882e:case 0x882f:case 0x8830:case 0x8831:case 0x8832:case 0x8833:case 0x8834:return 1;
    /* Scalar depth state saved/restored by native WebKit's WebGL backend.
     * Keep native boolean/double conversion; these are not depth-range pairs. */
    case GL_DEPTH_WRITEMASK:case GL_DEPTH_CLEAR_VALUE:return 1;
    /* Actual iDVD/Quartz Composer state-save queries. Both are scalar enums
     * in OpenGL 2.1; retain native validation/conversion rather than inferring
     * a value from our setter cache or guessing all unknown pnames scalar. */
    case GL_FRONT_FACE:case GL_LIGHT_MODEL_COLOR_CONTROL:return 1;
    /* Native8R4088 CGLSetPBuffer queries this APPLE_float_pixels boolean
     * before attaching the native pbuffer. Forward the actual host query;
     * do not guess a float/non-float value from the requested pixel format. */
    case GL_COLOR_FLOAT_APPLE:return 1;
    case GL_UNPACK_SWAP_BYTES:case GL_UNPACK_LSB_FIRST:
    case GL_PACK_SWAP_BYTES:case GL_PACK_LSB_FIRST:
    /* Scalar since GL 1.2, also queried while saving/restoring Core Image's
     * pixel-store state around 2D effects. Unknown pnames still fail closed;
     * these are not texture dimensions or variable-length vector results. */
    case GL_PACK_IMAGE_HEIGHT:case GL_PACK_SKIP_IMAGES:
    case GL_UNPACK_IMAGE_HEIGHT:case GL_UNPACK_SKIP_IMAGES:
    case GL_RED_SCALE:case GL_RED_BIAS:case GL_GREEN_SCALE:case GL_GREEN_BIAS:
    case GL_BLUE_SCALE:case GL_BLUE_BIAS:case GL_ALPHA_SCALE:case GL_ALPHA_BIAS:
    case GL_DEPTH_SCALE:case GL_DEPTH_BIAS:case GL_MAP_COLOR:case GL_MAP_STENCIL:
    case GL_INDEX_SHIFT:case GL_INDEX_OFFSET:
    case GL_RENDER_MODE: /* Native CGL queries this when switching recovery screens. */
    case GL_SELECTION_BUFFER_SIZE:case GL_FEEDBACK_BUFFER_SIZE:case GL_FEEDBACK_BUFFER_TYPE:
    case GL_LIST_BASE:case GL_LIST_INDEX:case GL_LIST_MODE:
    case GL_ATTRIB_STACK_DEPTH:case GL_MODELVIEW_STACK_DEPTH:
    case 0x8640:case 0x864b:case 0x8804: /* ARB program stack/error/enabled */
    case GL_PROJECTION_STACK_DEPTH:case GL_TEXTURE_STACK_DEPTH:return 1;
    case 0x821b:case 0x821c:case 0x821d:case 0x821e:case 0x9126:return 1; /* core version, extensions, flags/profile */
    case 0x85b5:return 1; /* VERTEX_ARRAY_BINDING(_APPLE) */
    case 0x8e28:return 1; /* TIMESTAMP */
    case 0x8a34:return 1; /* UNIFORM_BUFFER_OFFSET_ALIGNMENT */
    case 0x8df9:case 0x8dfa:return 1; /* NUM_SHADER_BINARY_FORMATS, SHADER_COMPILER */
    case 0x87fe:return 1; /* NUM_PROGRAM_BINARY_FORMATS */
    case 0x8c36:case 0x8c37:return 1; /* SAMPLE_SHADING, MIN_SAMPLE_SHADING_VALUE */
    case 0x8919:return 1; /* SAMPLER_BINDING */
    case 0x86a4:case 0x86a5:case 0x86a6:case 0x86a7:return 1; /* ARB vertex blending */
    case 0x8de2:case 0x8de3:case 0x8de4:case 0x8ded:case 0x8def:return 1; /* EXT bindable uniform */
    case 0x8a26:case 0x8a27:return 1; /* APPLE per-vertex point size */
    case 0x8a10:return 2; /* MIN_PBUFFER_VIEWPORT_DIMS_APPLE */
    default:break;
    }
    switch(key) {
#include "direct-get-counts.inc"
    default:return 0;
    }
}
static uint32_t DGStateArrayCountKey(uint32_t key)
{
    return key==0x8df8 ? 0x8df9 : key==0x87ff ? 0x87fe : key==0x86a8 ? 0x86a4 :
        key==0x86a3 ? 0x86a2 : 0;
}
static size_t DGStateGetBytes(uint32_t key,int32_t binary_count,size_t element)
{
    if(DGStateArrayCountKey(key))return DGQueryArrayBytes(binary_count,element);
    size_t n=DGGetCount(key);
    return n ? n*element : SIZE_MAX;
}
static size_t DGLightingCount(unsigned kind,uint32_t key)
{
    if(kind==0) { /* glLight[fi]v / glGetLight[fi]v */
        switch(key) {
        case GL_AMBIENT:case GL_DIFFUSE:case GL_SPECULAR:case GL_POSITION:return 4;
        case GL_SPOT_DIRECTION:return 3;
        case GL_SPOT_EXPONENT:case GL_SPOT_CUTOFF:case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:case GL_QUADRATIC_ATTENUATION:return 1;
        default:return 0;
        }
    }
    if(kind==1) { /* glMaterial[fi]v / glGetMaterial[fi]v */
        switch(key) {
        case GL_AMBIENT:case GL_DIFFUSE:case GL_SPECULAR:case GL_EMISSION:case GL_AMBIENT_AND_DIFFUSE:return 4;
        case GL_COLOR_INDEXES:return 3;
        case GL_SHININESS:return 1;
        default:return 0;
        }
    }
    if(kind==2) { /* glLightModel[fi]v */
        switch(key) {
        case GL_LIGHT_MODEL_AMBIENT:return 4;
        case GL_LIGHT_MODEL_LOCAL_VIEWER:case GL_LIGHT_MODEL_TWO_SIDE:case GL_LIGHT_MODEL_COLOR_CONTROL:return 1;
        default:return 0;
        }
    }
    switch(key) { /* glFog[fi]v */
    case GL_FOG_COLOR:return 4;
    case GL_FOG_MODE:case GL_FOG_DENSITY:case GL_FOG_START:case GL_FOG_END:case GL_FOG_INDEX:case GL_FOG_COORD_SRC:return 1;
    default:return 0;
    }
}
static size_t DGPixelSize(uint32_t format,uint32_t type)
{
    /* APPLE_ycbcr_422: both 2vuy/yuvs orderings use two bytes per pixel,
     * not two independently stored components. Native GL does conversion. */
    if(type==0x85ba || type==0x85bb)return format==0x85b9 ? 2 : 0;
    /* Packed encodings have one storage unit per pixel; the actual native
     * call still validates whether format/type is a legal pairing. */
    switch(type) {
    case 0x8032:case 0x8362:return 1; /* 3_3_2 and 2_3_3_REV */
    case 0x8035:case 0x8367:case 0x8036:case 0x8368:case 0x84fa:
    case 0x8c3b:case 0x8c3e:return 4; /* includes 10F_11F_11F and 5_9_9_9 */
    case 0x8363:case 0x8364:case 0x8033:case 0x8365:case 0x8034:case 0x8366:return 2;
    case 0x8dad:return 8; /* FLOAT_32_UNSIGNED_INT_24_8_REV */
    }
    size_t components=0,bytes=0;
    switch(format) {
    case 0x1900:case 0x1901:case 0x1902:case 0x1903:case 0x1904:case 0x1905:case 0x1906:case 0x1909:
    case 0x8d94:case 0x8d95:case 0x8d96:case 0x8d97:case 0x8d9c:components=1;break;
    case 0x190a:case 0x8227:case 0x8228:case 0x8d9d:components=2;break;
    case 0x1907:case 0x80e0:case 0x8d98:case 0x8d9a:components=3;break;
    case 0x1908:case 0x80e1:case 0x8000:case 0x8d99:case 0x8d9b:components=4;break;
    }
    switch(type) {
    case 0x1400:case 0x1401:bytes=1;break;
    case 0x1402:case 0x1403:case 0x140b:bytes=2;break;
    case 0x1404:case 0x1405:case 0x1406:bytes=4;break;
    }
    return components*bytes;
}
static size_t DGRasterPixelBits(GLenum format,GLenum type)
{
    if(type==GL_BITMAP)return format==GL_COLOR_INDEX || format==GL_STENCIL_INDEX ? 1 : 0;
    return 8*DGPixelSize(format,type);
}
/* Compute the final byte touched, including a partially used bitmap byte.
 * A PBO does not carry image data on the wire and is not packet-size limited. */
static uint64_t DGPixelLayoutSpan(int width,int height,size_t bits,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    if(!bits || width<0 || height<0 || row<0 || skiprows<0 || skippixels<0 || rowbytes<0 ||
       (alignment!=1 && alignment!=2 && alignment!=4 && alignment!=8))return UINT64_MAX;
    if(!width || !height)return 0;
    uint64_t row_size=((uint64_t)(row?row:width)*bits+7)/8;
    uint64_t stride=rowbytes?(uint64_t)rowbytes:((row_size+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)skiprows+height-1;
    uint64_t tail=(((uint64_t)skippixels+width)*bits+7)/8;
    if(rows && stride>(UINT64_MAX-tail)/rows)return UINT64_MAX;
    return rows*stride+tail;
}
static uint64_t DGPixelSpanWide(int width,int height,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    return DGPixelLayoutSpan(width,height,DGRasterPixelBits(format,type),
        row,alignment,skiprows,skippixels,rowbytes);
}
static size_t DGPixelSpan(int width,int height,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    uint64_t span=DGPixelSpanWide(width,height,format,type,row,alignment,skiprows,skippixels,rowbytes);
    return span>DG_MAX_BYTES?SIZE_MAX:(size_t)span;
}

/* --- evaluators, call lists, pixel maps, imaging parameters, 3D pixel spans: more of
 * passthrough-opengl-wire.h, unmodified (added for the full gl* export list). */
enum DGImagingParameters {
    DG_COLOR_TABLE_SET, DG_COLOR_TABLE_GET, DG_CONVOLUTION_SET,
    DG_CONVOLUTION_GET, DG_HISTOGRAM_GET, DG_MINMAX_GET,
};
static uint64_t DGPixelSpan3DWide(int width,int height,int depth,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes,int imageheight,int skipimages)
{
    if(depth<0 || imageheight<0 || skipimages<0)return UINT64_MAX;
    uint64_t bytes=DGPixelSpanWide(width,height,format,type,row,alignment,skiprows,skippixels,rowbytes);
    if(bytes==UINT64_MAX || !width || !height || !depth)return bytes==UINT64_MAX ? UINT64_MAX : 0;
    uint64_t bits=DGRasterPixelBits(format,type);
    uint64_t stride=rowbytes ? (uint64_t)rowbytes :
        (((((uint64_t)(row ? row : width)*bits+7)/8)+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)(imageheight ? imageheight : height);
    uint64_t images=(uint64_t)skipimages+(uint64_t)depth-1;
    if(images && (rows>UINT64_MAX/images || stride>UINT64_MAX/(images*rows)))return UINT64_MAX;
    uint64_t prefix=images*rows*stride;
    return prefix>UINT64_MAX-bytes ? UINT64_MAX : prefix+bytes;
}

static size_t DGPixelSpan3D(int width,int height,int depth,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes,int imageheight,int skipimages)
{
    uint64_t span=DGPixelSpan3DWide(width,height,depth,format,type,row,alignment,skiprows,skippixels,rowbytes,imageheight,skipimages);
    return span>DG_MAX_BYTES?SIZE_MAX:(size_t)span;
}

static unsigned DGMapDimensions(uint32_t target)
{
    if(target>=0x0d90 && target<=0x0d98)return 1;
    if(target>=0x0db0 && target<=0x0db8)return 2;
    return 0;
}

static unsigned DGMapComponents(uint32_t target)
{
    static const unsigned char components[]={4,1,3,1,2,3,4,3,4};
    unsigned dimensions=DGMapDimensions(target);
    return dimensions?components[target-(dimensions==2?0x0db0:0x0d90)]:0;
}

static size_t DGEvaluatorCoefficientBytes(unsigned width,int32_t uorder,int32_t vorder,size_t element)
{
    if(width<1 || width>4 || uorder<1 || vorder<1 || !element ||
       element>DG_MAX_BYTES/width)return SIZE_MAX;
    uint64_t count=(uint64_t)uorder*vorder;
    return count>DG_MAX_BYTES/element/width ? SIZE_MAX : (size_t)count*width*element;
}

static size_t DGMapQueryBytes(uint32_t target,uint32_t query,
    int32_t uorder,int32_t vorder,size_t element)
{
    unsigned dimensions=DGMapDimensions(target);
    if(!dimensions || !element || element>DG_MAX_BYTES/4)return SIZE_MAX;
    if(query==0x0a01)return dimensions*element;   /* GL_ORDER */
    if(query==0x0a02)return dimensions*2*element; /* GL_DOMAIN */
    if(query!=0x0a00)return SIZE_MAX;
    return DGEvaluatorCoefficientBytes(DGMapComponents(target),uorder,dimensions==2?vorder:1,element);
}

static size_t DGCallListsBytes(int32_t count,uint32_t type)
{
    if(count<=0)return 0;
    size_t element;
    switch(type){
    case 0x1400:case 0x1401:element=1;break;
    case 0x1402:case 0x1403:case 0x1407:element=2;break;
    case 0x1408:element=3;break;
    case 0x1404:case 0x1405:case 0x1406:case 0x1409:element=4;break;
    default:return 0;
    }
    return DGArrayBytes(count,element);
}

static size_t DGImagingParameterCount(enum DGImagingParameters operation,uint32_t key)
{
    switch(operation) {
    case DG_COLOR_TABLE_SET:case DG_COLOR_TABLE_GET:
        if(key==GL_COLOR_TABLE_SCALE || key==GL_COLOR_TABLE_BIAS)return 4;
        return operation==DG_COLOR_TABLE_GET && key>=GL_COLOR_TABLE_FORMAT &&
            key<=GL_COLOR_TABLE_INTENSITY_SIZE;
    case DG_CONVOLUTION_SET:case DG_CONVOLUTION_GET:
        if(key==GL_CONVOLUTION_FILTER_SCALE || key==GL_CONVOLUTION_FILTER_BIAS ||
           key==GL_CONVOLUTION_BORDER_COLOR)return 4;
        if(key==GL_CONVOLUTION_BORDER_MODE)return 1;
        return operation==DG_CONVOLUTION_GET && key>=GL_CONVOLUTION_FORMAT &&
            key<=GL_MAX_CONVOLUTION_HEIGHT;
    case DG_HISTOGRAM_GET:return key>=GL_HISTOGRAM_WIDTH && key<=GL_HISTOGRAM_SINK;
    case DG_MINMAX_GET:return key==GL_MINMAX_FORMAT || key==GL_MINMAX_SINK;
    }
    return 0;
}

static GLenum DGPixelMapSizeKey(GLenum map)
{
    return map>=GL_PIXEL_MAP_I_TO_I && map<=GL_PIXEL_MAP_A_TO_A ?
        GL_PIXEL_MAP_I_TO_I_SIZE+map-GL_PIXEL_MAP_I_TO_I : 0;
}

static int DGPixelMapCountValid(GLenum map,GLsizei count,GLint maximum)
{
    /* Only index-addressed maps (I_TO_I through I_TO_A) require 2^n. */
    return count>0 && count<=maximum &&
        (map>=GL_PIXEL_MAP_R_TO_R || !(count&(count-1)));
}

static int DGDrawPixelsNoInput(GLsizei width,GLsizei height,GLenum format,GLenum type)
{
    if(width<0 || height<0 || !DGRasterPixelBits(format,type))return 1;
    switch(type){
    case 0x8032:case 0x8362:case 0x8363:case 0x8364:
        return format!=GL_RGB;
    case 0x8033:case 0x8365:case 0x8034:case 0x8366:
    case 0x8035:case 0x8367:case 0x8036:case 0x8368:
        return format!=GL_RGBA && format!=GL_BGRA;
    default:return 0;
    }
}

enum DGImagingTransfer {
    DG_IMAGING_TABLE, DG_IMAGING_SUBTABLE, DG_IMAGING_GET_TABLE,
    DG_IMAGING_FILTER1D, DG_IMAGING_FILTER2D, DG_IMAGING_GET_FILTER,
    DG_IMAGING_SEPARABLE, DG_IMAGING_GET_SEPARABLE, DG_IMAGING_GET_HISTOGRAM,
    DG_IMAGING_GET_MINMAX,
};
#endif
