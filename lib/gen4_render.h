#ifndef GEN4_RENDER_H
#define GEN4_RENDER_H

#include <stdint.h>

#define GEN4_3D(Pipeline,Opcode,Subopcode) ((3 << 29) | \
					   ((Pipeline) << 27) | \
					   ((Opcode) << 24) | \
					   ((Subopcode) << 16))

#define GEN4_URB_FENCE				GEN4_3D(0, 0, 0)
#define GEN4_CS_URB_STATE			GEN4_3D(0, 0, 1)
#define GEN4_STATE_BASE_ADDRESS			GEN4_3D(0, 1, 1)
#define GEN4_STATE_SIP				GEN4_3D(0, 1, 2)
#define GEN4_PIPELINE_SELECT			GEN4_3D(0, 1, 4)

#define G4X_PIPELINE_SELECT			GEN4_3D(1, 1, 4)

#define GEN4_3DSTATE_VERTEX_BUFFERS		GEN4_3D(3, 0, 8)
#define GEN4_3DSTATE_VERTEX_ELEMENTS		GEN4_3D(3, 0, 9)

#define GEN4_3DSTATE_DRAWING_RECTANGLE		GEN4_3D(3, 1, 0)
#define GEN4_3DSTATE_DEPTH_BUFFER		GEN4_3D(3, 1, 5)
# define GEN4_3DSTATE_DEPTH_BUFFER_TYPE_SHIFT	29
# define GEN4_3DSTATE_DEPTH_BUFFER_FORMAT_SHIFT	18

#define GEN4_3DPRIMITIVE			GEN4_3D(3, 3, 0)

#define GEN4_3DSTATE_CLEAR_PARAMS		GEN4_3D(3, 1, 0x10)
/* DW1 */
# define GEN4_3DSTATE_DEPTH_CLEAR_VALID		(1 << 15)

#define PIPELINE_SELECT_3D		0
#define PIPELINE_SELECT_MEDIA		1

#define UF0_CS_REALLOC			(1 << 13)
#define UF0_VFE_REALLOC			(1 << 12)
#define UF0_SF_REALLOC			(1 << 11)
#define UF0_CLIP_REALLOC		(1 << 10)
#define UF0_GS_REALLOC			(1 << 9)
#define UF0_VS_REALLOC			(1 << 8)
#define UF1_CLIP_FENCE_SHIFT		20
#define UF1_GS_FENCE_SHIFT		10
#define UF1_VS_FENCE_SHIFT		0
#define UF2_CS_FENCE_SHIFT		20
#define UF2_VFE_FENCE_SHIFT		10
#define UF2_SF_FENCE_SHIFT		0

/* for GEN4_STATE_BASE_ADDRESS */
#define BASE_ADDRESS_MODIFY		(1 << 0)

/* VERTEX_BUFFER_STATE Structure */
#define VB0_BUFFER_INDEX_SHIFT	27
#define VB0_VERTEXDATA			(0 << 26) /* for GEN5 */
#define VB0_INSTANCEDATA		(1 << 26) /* for GEN5 */
#define VB0_BUFFER_PITCH_SHIFT		0

/* VERTEX_ELEMENT_STATE Structure */
#define VE0_VERTEX_BUFFER_INDEX_SHIFT	27
#define VE0_VALID			(1 << 26)
#define VE0_FORMAT_SHIFT		16
#define VE0_OFFSET_SHIFT		0
#define VE1_VFCOMPONENT_0_SHIFT		28
#define VE1_VFCOMPONENT_1_SHIFT		24
#define VE1_VFCOMPONENT_2_SHIFT		20
#define VE1_VFCOMPONENT_3_SHIFT		16
#define VE1_DESTINATION_ELEMENT_OFFSET_SHIFT	0

/* 3DPRIMITIVE bits */
#define GEN4_3DPRIMITIVE_VERTEX_SEQUENTIAL (0 << 15)
#define GEN4_3DPRIMITIVE_VERTEX_RANDOM	  (1 << 15)
#define GEN4_3DPRIMITIVE_TOPOLOGY_SHIFT	  10

#define GEN4_3DSTATE_BINDING_TABLE_POINTERS	GEN4_3D(3, 0, 1)

#define GEN4_3DSTATE_VERTEX_ELEMENTS		GEN4_3D(3, 0, 9)

# define GEN4_CULLMODE_BOTH			0
# define GEN4_CULLMODE_NONE			1
# define GEN4_CULLMODE_FRONT			2
# define GEN4_CULLMODE_BACK			3

#define PIPELINE_SELECT_3D		0
#define PIPELINE_SELECT_MEDIA		1

#define UF0_CS_REALLOC			(1 << 13)
#define UF0_VFE_REALLOC			(1 << 12)
#define UF0_SF_REALLOC			(1 << 11)
#define UF0_CLIP_REALLOC		(1 << 10)
#define UF0_GS_REALLOC			(1 << 9)
#define UF0_VS_REALLOC			(1 << 8)
#define UF1_CLIP_FENCE_SHIFT		20
#define UF1_GS_FENCE_SHIFT		10
#define UF1_VS_FENCE_SHIFT		0
#define UF2_CS_FENCE_SHIFT		20
#define UF2_VFE_FENCE_SHIFT		10
#define UF2_SF_FENCE_SHIFT		0

/* for GEN4_STATE_BASE_ADDRESS */
#define BASE_ADDRESS_MODIFY		(1 << 0)

/* for GEN4_3DSTATE_PIPELINED_POINTERS */
#define GEN4_3DSTATE_PIPELINED_POINTERS	GEN4_3D(3, 0, 0)
#define GEN4_GS_DISABLE			0
#define GEN4_GS_ENABLE			1
#define GEN4_CLIP_DISABLE		0
#define GEN4_CLIP_ENABLE		1

#define _3DPRIM_POINTLIST         0x01
#define _3DPRIM_LINELIST          0x02
#define _3DPRIM_LINESTRIP         0x03
#define _3DPRIM_TRILIST           0x04
#define _3DPRIM_TRISTRIP          0x05
#define _3DPRIM_TRIFAN            0x06
#define _3DPRIM_QUADLIST          0x07
#define _3DPRIM_QUADSTRIP         0x08
#define _3DPRIM_LINELIST_ADJ      0x09
#define _3DPRIM_LINESTRIP_ADJ     0x0A
#define _3DPRIM_TRILIST_ADJ       0x0B
#define _3DPRIM_TRISTRIP_ADJ      0x0C
#define _3DPRIM_TRISTRIP_REVERSE  0x0D
#define _3DPRIM_POLYGON           0x0E
#define _3DPRIM_RECTLIST          0x0F
#define _3DPRIM_LINELOOP          0x10
#define _3DPRIM_POINTLIST_BF      0x11
#define _3DPRIM_LINESTRIP_CONT    0x12
#define _3DPRIM_LINESTRIP_BF      0x13
#define _3DPRIM_LINESTRIP_CONT_BF 0x14
#define _3DPRIM_TRIFAN_NOSTIPPLE  0x15

#define GEN4_BORDER_COLOR_MODE_DEFAULT	0
#define GEN4_BORDER_COLOR_MODE_LEGACY	1

#define GEN4_DEPTHFORMAT_D32_FLOAT_S8X24_UINT     0
#define GEN4_DEPTHFORMAT_D32_FLOAT                1
#define GEN4_DEPTHFORMAT_D24_UNORM_S8_UINT        2
#define GEN4_DEPTHFORMAT_D24_UNORM_X8_UINT        3
#define GEN4_DEPTHFORMAT_D16_UNORM                5

#define GEN4_MAPFILTER_NEAREST        0
#define GEN4_MAPFILTER_LINEAR         1
#define GEN4_MAPFILTER_ANISOTROPIC    2
#define GEN4_MAPFILTER_MONO           6

#define GEN4_MIPFILTER_NONE        0
#define GEN4_MIPFILTER_NEAREST     1
#define GEN4_MIPFILTER_LINEAR      3

#define GEN4_PREFILTER_ALWAYS     0
#define GEN4_PREFILTER_NEVER      1
#define GEN4_PREFILTER_LESS       2
#define GEN4_PREFILTER_EQUAL      3
#define GEN4_PREFILTER_LEQUAL     4
#define GEN4_PREFILTER_GREATER    5
#define GEN4_PREFILTER_NOTEQUAL   6
#define GEN4_PREFILTER_GEQUAL     7

#define GEN4_SURFACEFORMAT_R32G32B32A32_FLOAT             0x000
#define GEN4_SURFACEFORMAT_R32G32B32A32_SINT              0x001
#define GEN4_SURFACEFORMAT_R32G32B32A32_UINT              0x002
#define GEN4_SURFACEFORMAT_R32G32B32A32_UNORM             0x003
#define GEN4_SURFACEFORMAT_R32G32B32A32_SNORM             0x004
#define GEN4_SURFACEFORMAT_R64G64_FLOAT                   0x005
#define GEN4_SURFACEFORMAT_R32G32B32X32_FLOAT             0x006
#define GEN4_SURFACEFORMAT_R32G32B32A32_SSCALED           0x007
#define GEN4_SURFACEFORMAT_R32G32B32A32_USCALED           0x008
#define GEN4_SURFACEFORMAT_R32G32B32_FLOAT                0x040
#define GEN4_SURFACEFORMAT_R32G32B32_SINT                 0x041
#define GEN4_SURFACEFORMAT_R32G32B32_UINT                 0x042
#define GEN4_SURFACEFORMAT_R32G32B32_UNORM                0x043
#define GEN4_SURFACEFORMAT_R32G32B32_SNORM                0x044
#define GEN4_SURFACEFORMAT_R32G32B32_SSCALED              0x045
#define GEN4_SURFACEFORMAT_R32G32B32_USCALED              0x046
#define GEN4_SURFACEFORMAT_R16G16B16A16_UNORM             0x080
#define GEN4_SURFACEFORMAT_R16G16B16A16_SNORM             0x081
#define GEN4_SURFACEFORMAT_R16G16B16A16_SINT              0x082
#define GEN4_SURFACEFORMAT_R16G16B16A16_UINT              0x083
#define GEN4_SURFACEFORMAT_R16G16B16A16_FLOAT             0x084
#define GEN4_SURFACEFORMAT_R32G32_FLOAT                   0x085
#define GEN4_SURFACEFORMAT_R32G32_SINT                    0x086
#define GEN4_SURFACEFORMAT_R32G32_UINT                    0x087
#define GEN4_SURFACEFORMAT_R32_FLOAT_X8X24_TYPELESS       0x088
#define GEN4_SURFACEFORMAT_X32_TYPELESS_G8X24_UINT        0x089
#define GEN4_SURFACEFORMAT_L32A32_FLOAT                   0x08A
#define GEN4_SURFACEFORMAT_R32G32_UNORM                   0x08B
#define GEN4_SURFACEFORMAT_R32G32_SNORM                   0x08C
#define GEN4_SURFACEFORMAT_R64_FLOAT                      0x08D
#define GEN4_SURFACEFORMAT_R16G16B16X16_UNORM             0x08E
#define GEN4_SURFACEFORMAT_R16G16B16X16_FLOAT             0x08F
#define GEN4_SURFACEFORMAT_A32X32_FLOAT                   0x090
#define GEN4_SURFACEFORMAT_L32X32_FLOAT                   0x091
#define GEN4_SURFACEFORMAT_I32X32_FLOAT                   0x092
#define GEN4_SURFACEFORMAT_R16G16B16A16_SSCALED           0x093
#define GEN4_SURFACEFORMAT_R16G16B16A16_USCALED           0x094
#define GEN4_SURFACEFORMAT_R32G32_SSCALED                 0x095
#define GEN4_SURFACEFORMAT_R32G32_USCALED                 0x096
#define GEN4_SURFACEFORMAT_B8G8R8A8_UNORM                 0x0C0
#define GEN4_SURFACEFORMAT_B8G8R8A8_UNORM_SRGB            0x0C1
#define GEN4_SURFACEFORMAT_R10G10B10A2_UNORM              0x0C2
#define GEN4_SURFACEFORMAT_R10G10B10A2_UNORM_SRGB         0x0C3
#define GEN4_SURFACEFORMAT_R10G10B10A2_UINT               0x0C4
#define GEN4_SURFACEFORMAT_R10G10B10_SNORM_A2_UNORM       0x0C5
#define GEN4_SURFACEFORMAT_R8G8B8A8_UNORM                 0x0C7
#define GEN4_SURFACEFORMAT_R8G8B8A8_UNORM_SRGB            0x0C8
#define GEN4_SURFACEFORMAT_R8G8B8A8_SNORM                 0x0C9
#define GEN4_SURFACEFORMAT_R8G8B8A8_SINT                  0x0CA
#define GEN4_SURFACEFORMAT_R8G8B8A8_UINT                  0x0CB
#define GEN4_SURFACEFORMAT_R16G16_UNORM                   0x0CC
#define GEN4_SURFACEFORMAT_R16G16_SNORM                   0x0CD
#define GEN4_SURFACEFORMAT_R16G16_SINT                    0x0CE
#define GEN4_SURFACEFORMAT_R16G16_UINT                    0x0CF
#define GEN4_SURFACEFORMAT_R16G16_FLOAT                   0x0D0
#define GEN4_SURFACEFORMAT_B10G10R10A2_UNORM              0x0D1
#define GEN4_SURFACEFORMAT_B10G10R10A2_UNORM_SRGB         0x0D2
#define GEN4_SURFACEFORMAT_R11G11B10_FLOAT                0x0D3
#define GEN4_SURFACEFORMAT_R32_SINT                       0x0D6
#define GEN4_SURFACEFORMAT_R32_UINT                       0x0D7
#define GEN4_SURFACEFORMAT_R32_FLOAT                      0x0D8
#define GEN4_SURFACEFORMAT_R24_UNORM_X8_TYPELESS          0x0D9
#define GEN4_SURFACEFORMAT_X24_TYPELESS_G8_UINT           0x0DA
#define GEN4_SURFACEFORMAT_L16A16_UNORM                   0x0DF
#define GEN4_SURFACEFORMAT_I24X8_UNORM                    0x0E0
#define GEN4_SURFACEFORMAT_L24X8_UNORM                    0x0E1
#define GEN4_SURFACEFORMAT_A24X8_UNORM                    0x0E2
#define GEN4_SURFACEFORMAT_I32_FLOAT                      0x0E3
#define GEN4_SURFACEFORMAT_L32_FLOAT                      0x0E4
#define GEN4_SURFACEFORMAT_A32_FLOAT                      0x0E5
#define GEN4_SURFACEFORMAT_B8G8R8X8_UNORM                 0x0E9
#define GEN4_SURFACEFORMAT_B8G8R8X8_UNORM_SRGB            0x0EA
#define GEN4_SURFACEFORMAT_R8G8B8X8_UNORM                 0x0EB
#define GEN4_SURFACEFORMAT_R8G8B8X8_UNORM_SRGB            0x0EC
#define GEN4_SURFACEFORMAT_R9G9B9E5_SHAREDEXP             0x0ED
#define GEN4_SURFACEFORMAT_B10G10R10X2_UNORM              0x0EE
#define GEN4_SURFACEFORMAT_L16A16_FLOAT                   0x0F0
#define GEN4_SURFACEFORMAT_R32_UNORM                      0x0F1
#define GEN4_SURFACEFORMAT_R32_SNORM                      0x0F2
#define GEN4_SURFACEFORMAT_R10G10B10X2_USCALED            0x0F3
#define GEN4_SURFACEFORMAT_R8G8B8A8_SSCALED               0x0F4
#define GEN4_SURFACEFORMAT_R8G8B8A8_USCALED               0x0F5
#define GEN4_SURFACEFORMAT_R16G16_SSCALED                 0x0F6
#define GEN4_SURFACEFORMAT_R16G16_USCALED                 0x0F7
#define GEN4_SURFACEFORMAT_R32_SSCALED                    0x0F8
#define GEN4_SURFACEFORMAT_R32_USCALED                    0x0F9
#define GEN4_SURFACEFORMAT_B5G6R5_UNORM                   0x100
#define GEN4_SURFACEFORMAT_B5G6R5_UNORM_SRGB              0x101
#define GEN4_SURFACEFORMAT_B5G5R5A1_UNORM                 0x102
#define GEN4_SURFACEFORMAT_B5G5R5A1_UNORM_SRGB            0x103
#define GEN4_SURFACEFORMAT_B4G4R4A4_UNORM                 0x104
#define GEN4_SURFACEFORMAT_B4G4R4A4_UNORM_SRGB            0x105
#define GEN4_SURFACEFORMAT_R8G8_UNORM                     0x106
#define GEN4_SURFACEFORMAT_R8G8_SNORM                     0x107
#define GEN4_SURFACEFORMAT_R8G8_SINT                      0x108
#define GEN4_SURFACEFORMAT_R8G8_UINT                      0x109
#define GEN4_SURFACEFORMAT_R16_UNORM                      0x10A
#define GEN4_SURFACEFORMAT_R16_SNORM                      0x10B
#define GEN4_SURFACEFORMAT_R16_SINT                       0x10C
#define GEN4_SURFACEFORMAT_R16_UINT                       0x10D
#define GEN4_SURFACEFORMAT_R16_FLOAT                      0x10E
#define GEN4_SURFACEFORMAT_I16_UNORM                      0x111
#define GEN4_SURFACEFORMAT_L16_UNORM                      0x112
#define GEN4_SURFACEFORMAT_A16_UNORM                      0x113
#define GEN4_SURFACEFORMAT_L8A8_UNORM                     0x114
#define GEN4_SURFACEFORMAT_I16_FLOAT                      0x115
#define GEN4_SURFACEFORMAT_L16_FLOAT                      0x116
#define GEN4_SURFACEFORMAT_A16_FLOAT                      0x117
#define GEN4_SURFACEFORMAT_R5G5_SNORM_B6_UNORM            0x119
#define GEN4_SURFACEFORMAT_B5G5R5X1_UNORM                 0x11A
#define GEN4_SURFACEFORMAT_B5G5R5X1_UNORM_SRGB            0x11B
#define GEN4_SURFACEFORMAT_R8G8_SSCALED                   0x11C
#define GEN4_SURFACEFORMAT_R8G8_USCALED                   0x11D
#define GEN4_SURFACEFORMAT_R16_SSCALED                    0x11E
#define GEN4_SURFACEFORMAT_R16_USCALED                    0x11F
#define GEN4_SURFACEFORMAT_R8_UNORM                       0x140
#define GEN4_SURFACEFORMAT_R8_SNORM                       0x141
#define GEN4_SURFACEFORMAT_R8_SINT                        0x142
#define GEN4_SURFACEFORMAT_R8_UINT                        0x143
#define GEN4_SURFACEFORMAT_A8_UNORM                       0x144
#define GEN4_SURFACEFORMAT_I8_UNORM                       0x145
#define GEN4_SURFACEFORMAT_L8_UNORM                       0x146
#define GEN4_SURFACEFORMAT_P4A4_UNORM                     0x147
#define GEN4_SURFACEFORMAT_A4P4_UNORM                     0x148
#define GEN4_SURFACEFORMAT_R8_SSCALED                     0x149
#define GEN4_SURFACEFORMAT_R8_USCALED                     0x14A
#define GEN4_SURFACEFORMAT_R1_UINT                        0x181
#define GEN4_SURFACEFORMAT_YCRCB_NORMAL                   0x182
#define GEN4_SURFACEFORMAT_YCRCB_SWAPUVY                  0x183
#define GEN4_SURFACEFORMAT_BC1_UNORM                      0x186
#define GEN4_SURFACEFORMAT_BC2_UNORM                      0x187
#define GEN4_SURFACEFORMAT_BC3_UNORM                      0x188
#define GEN4_SURFACEFORMAT_BC4_UNORM                      0x189
#define GEN4_SURFACEFORMAT_BC5_UNORM                      0x18A
#define GEN4_SURFACEFORMAT_BC1_UNORM_SRGB                 0x18B
#define GEN4_SURFACEFORMAT_BC2_UNORM_SRGB                 0x18C
#define GEN4_SURFACEFORMAT_BC3_UNORM_SRGB                 0x18D
#define GEN4_SURFACEFORMAT_MONO8                          0x18E
#define GEN4_SURFACEFORMAT_YCRCB_SWAPUV                   0x18F
#define GEN4_SURFACEFORMAT_YCRCB_SWAPY                    0x190
#define GEN4_SURFACEFORMAT_DXT1_RGB                       0x191
#define GEN4_SURFACEFORMAT_FXT1                           0x192
#define GEN4_SURFACEFORMAT_R8G8B8_UNORM                   0x193
#define GEN4_SURFACEFORMAT_R8G8B8_SNORM                   0x194
#define GEN4_SURFACEFORMAT_R8G8B8_SSCALED                 0x195
#define GEN4_SURFACEFORMAT_R8G8B8_USCALED                 0x196
#define GEN4_SURFACEFORMAT_R64G64B64A64_FLOAT             0x197
#define GEN4_SURFACEFORMAT_R64G64B64_FLOAT                0x198
#define GEN4_SURFACEFORMAT_BC4_SNORM                      0x199
#define GEN4_SURFACEFORMAT_BC5_SNORM                      0x19A
#define GEN4_SURFACEFORMAT_R16G16B16_UNORM                0x19C
#define GEN4_SURFACEFORMAT_R16G16B16_SNORM                0x19D
#define GEN4_SURFACEFORMAT_R16G16B16_SSCALED              0x19E
#define GEN4_SURFACEFORMAT_R16G16B16_USCALED              0x19F

#define GEN4_SURFACERETURNFORMAT_FLOAT32  0
#define GEN4_SURFACERETURNFORMAT_S1       1

#define GEN4_SURFACE_1D      0
#define GEN4_SURFACE_2D      1
#define GEN4_SURFACE_3D      2
#define GEN4_SURFACE_CUBE    3
#define GEN4_SURFACE_BUFFER  4
#define GEN4_SURFACE_NULL    7

#define GEN4_TEXCOORDMODE_WRAP            0
#define GEN4_TEXCOORDMODE_MIRROR          1
#define GEN4_TEXCOORDMODE_CLAMP           2
#define GEN4_TEXCOORDMODE_CUBE            3
#define GEN4_TEXCOORDMODE_CLAMP_BORDER    4
#define GEN4_TEXCOORDMODE_MIRROR_ONCE     5

#define GEN4_VFCOMPONENT_NOSTORE      0
#define GEN4_VFCOMPONENT_STORE_SRC    1
#define GEN4_VFCOMPONENT_STORE_0      2
#define GEN4_VFCOMPONENT_STORE_1_FLT  3
#define GEN4_VFCOMPONENT_STORE_1_INT  4
#define GEN4_VFCOMPONENT_STORE_VID    5
#define GEN4_VFCOMPONENT_STORE_IID    6
#define GEN4_VFCOMPONENT_STORE_PID    7

#define GEN4_LOD_PRECLAMP_D3D	0
#define GEN4_LOD_PRECLAMP_OGL	1

/* The hardware supports two different modes for border color. The
 * default (OpenGL) mode uses floating-point color channels, while the
 * legacy mode uses 4 bytes.
 *
 * More significantly, the legacy mode respects the components of the
 * border color for channels not present in the source, (whereas the
 * default mode will ignore the border color's alpha channel and use
 * alpha==1 for an RGB source, for example).
 *
 * The legacy mode matches the semantics specified by the Render
 * extension.
 */
struct gen4_sampler_default_border_color {
   float color[4];
};

struct gen4_sampler_legacy_border_color {
   uint8_t color[4];
};

struct gen4_sampler_state {
   struct {
	   uint32_t shadow_function:3;
	   uint32_t lod_bias:11;
	   uint32_t min_filter:3;
	   uint32_t mag_filter:3;
	   uint32_t mip_filter:2;
	   uint32_t base_level:5;
	   uint32_t pad0:1;
	   uint32_t lod_preclamp:1;
	   uint32_t border_color_mode:1;
	   uint32_t pad1:1;
	   uint32_t disable:1;
   } ss0;

   struct {
	   uint32_t r_wrap_mode:3;
	   uint32_t t_wrap_mode:3;
	   uint32_t s_wrap_mode:3;
	   uint32_t cube_ctlr_mode:1;
	   uint32_t pad:2;
	   uint32_t max_lod:10;
	   uint32_t min_lod:10;
   } ss1;

   struct {
	   uint32_t pad:5;
	   uint32_t border_color_pointer:27;
   } ss2;

   struct {
	   uint32_t pad:13;
	   uint32_t address_rounding_enable:6;
	   uint32_t max_aniso:3;
	   uint32_t chroma_key_mode:1;
	   uint32_t chroma_key_index:2;
	   uint32_t chroma_key_enable:1;
	   uint32_t monochrome_filter_width:3;
	   uint32_t monochrome_filter_height:3;
   } ss3;
};

typedef enum {
	SAMPLER_FILTER_NEAREST = 0,
	SAMPLER_FILTER_BILINEAR,
	FILTER_COUNT
} sampler_filter_t;

typedef enum {
	SAMPLER_EXTEND_NONE = 0,
	SAMPLER_EXTEND_REPEAT,
	SAMPLER_EXTEND_PAD,
	SAMPLER_EXTEND_REFLECT,
	EXTEND_COUNT
} sampler_extend_t;

struct gen4_surface_state {
	struct {
		unsigned int cube_pos_z:1;
		unsigned int cube_neg_z:1;
		unsigned int cube_pos_y:1;
		unsigned int cube_neg_y:1;
		unsigned int cube_pos_x:1;
		unsigned int cube_neg_x:1;
		unsigned int media_boundary_pixel_mode:2;
		unsigned int render_cache_read_mode:1;
		unsigned int cube_corner_mode:1;
		unsigned int mipmap_layout_mode:1;
		unsigned int vert_line_stride_ofs:1;
		unsigned int vert_line_stride:1;
		unsigned int color_blend:1;
		unsigned int writedisable_blue:1;
		unsigned int writedisable_green:1;
		unsigned int writedisable_red:1;
		unsigned int writedisable_alpha:1;
		unsigned int surface_format:9;
		unsigned int data_return_format:1;
		unsigned int pad0:1;
		unsigned int surface_type:3;
	} ss0;

	struct {
		unsigned int base_addr;
	} ss1;

	struct {
		unsigned int render_target_rotation:2;
		unsigned int mip_count:4;
		unsigned int width:13;
		unsigned int height:13;
	} ss2;

	struct {
		unsigned int tile_walk:1;
		unsigned int tiled_surface:1;
		unsigned int pad0:1;
		unsigned int pitch:17;
		unsigned int pad1:1;
		unsigned int depth:11;
	} ss3;

	struct {
		unsigned int pad:8;
		unsigned int render_target_view_extent:9;
		unsigned int min_array_elt:11;
		unsigned int min_lod:4;
	} ss4;

	struct {
		unsigned int pad:20;
		unsigned int y_offset:4;
		unsigned int pad1:1;
		unsigned int x_offset:7;
	} ss5;
};

struct gen4_cc_viewport {
	float min_depth;
	float max_depth;
};

struct gen4_vs_state {
	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer:26;
	} vs0;

	struct {
		unsigned int pad0:7;
		unsigned int sw_exception_enable:1;
		unsigned int pad1:3;
		unsigned int mask_stack_exception_enable:1;
		unsigned int pad2:1;
		unsigned int illegal_op_exception_enable:1;
		unsigned int pad3:2;
		unsigned int floating_point_mode:1;
		unsigned int thread_priority:1;
		unsigned int binding_table_entry_count:8;
		unsigned int pad4:5;
		unsigned int single_program_flow:1;
	} vs1;

	struct {
		unsigned int per_thread_scratch_space:4;
		unsigned int pad0:6;
		unsigned int scratch_space_pointer:22;
	} vs2;

	struct {
		unsigned int dispatch_grf_start_reg:4;
		unsigned int urb_entry_read_offset:6;
		unsigned int pad0:1;
		unsigned int urb_entry_read_length:6;
		unsigned int pad1:1;
		unsigned int const_urb_entry_read_offset:6;
		unsigned int pad2:1;
		unsigned int const_urb_entry_read_length:6;
		unsigned int pad3:1;
	} vs3;

	struct {
		unsigned int pad0:10;
		unsigned int stats_enable:1;
		unsigned int nr_urb_entries:7;
		unsigned int pad1:1;
		unsigned int urb_entry_allocation_size:5;
		unsigned int pad2:1;
		unsigned int max_threads:6;
		unsigned int pad3:1;
	} vs4;

	struct {
		unsigned int sampler_count:3;
		unsigned int pad:2;
		unsigned int sampler_state_pointer:27;
	} vs5;

	struct {
		unsigned int vs_enable:1;
		unsigned int vert_cache_disable:1;
		unsigned int pad:30;
	} vs6;
};

struct gen4_sf_state {
	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer:26;
	} sf0;

	struct {
		unsigned int barycentric_interp:1; /* ilk */
		unsigned int pad0:6;
		unsigned int sw_exception_enable:1;
		unsigned int pad1:3;
		unsigned int mask_stack_exception_enable:1;
		unsigned int pad2:1;
		unsigned int illegal_op_exception_enable:1;
		unsigned int pad3:2;
		unsigned int floating_point_mode:1;
		unsigned int thread_priority:1;
		unsigned int binding_table_entry_count:8;
		unsigned int pad4:6;
	} sf1;

	struct {
		unsigned int per_thread_scratch_space:4;
		unsigned int pad0:6;
		unsigned int scratch_space_pointer:22;
	} sf2;

	struct {
		unsigned int dispatch_grf_start_reg:4;
		unsigned int urb_entry_read_offset:6;
		unsigned int pad0:1;
		unsigned int urb_entry_read_length:7;
		unsigned int const_urb_entry_read_offset:6;
		unsigned int pad1:1;
		unsigned int const_urb_entry_read_length:6;
		unsigned int pad2:1;
	} sf3;

	struct {
		unsigned int pad0:10;
		unsigned int stats_enable:1;
		unsigned int nr_urb_entries:8;
		unsigned int urb_entry_allocation_size:6;
		unsigned int max_threads:6;
		unsigned int pad2:1;
	} sf4;

	struct {
		unsigned int front_winding:1;
		unsigned int viewport_transform:1;
		unsigned int pad:3;
		unsigned int sf_viewport_state_offset:27;
	} sf5;

	struct {
		unsigned int pad:9;
		unsigned int dest_org_vbias:4;
		unsigned int dest_org_hbias:4;
		unsigned int scissor:1;
		unsigned int disable_2x2_trifilter:1;
		unsigned int disable_zero_trifilter:1;
		unsigned int point_rast_rule:2;
		unsigned int line_endcap_aa_region_width:2;
		unsigned int line_width:4;
		unsigned int fast_scissor_disable:1;
		unsigned int cull_mode:2;
		unsigned int aa_enable:1;
	} sf6;

	struct {
		unsigned int point_size:11;
		unsigned int use_point_size_state:1;
		unsigned int subpixel_precision:1;
		unsigned int sprite_point:1;
		unsigned int aa_line_dist_mode:1;
		unsigned int pad:10;
		unsigned int trifan_pv:2;
		unsigned int linestrip_pv:2;
		unsigned int tristrip_pv:2;
		unsigned int line_last_pixel_enable:1;
	} sf7;
};

struct gen4_wm_state {
	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer:26;
	} wm0;

	struct {
		unsigned int pad0:1;
		unsigned int sw_exception_enable:1;
		unsigned int mask_stack_exception_enable:1;
		unsigned int pad2:1;
		unsigned int illegal_op_exception_enable:1;
		unsigned int pad3:3;
		unsigned int depth_coeff_urb_read_offset:6;
		unsigned int pad4:2;
		unsigned int floating_point_mode:1;
		unsigned int thread_priority:1;
		unsigned int binding_table_entry_count:8;
		unsigned int pad5:5;
		unsigned int single_program_flow:1;
	} wm1;

	struct {
		unsigned int per_thread_scratch_space:4;
		unsigned int pad0:6;
		unsigned int scratch_space_pointer:22;
	} wm2;

	struct {
		unsigned int dispatch_grf_start_reg:4;
		unsigned int urb_entry_read_offset:6;
		unsigned int pad0:1;
		unsigned int urb_entry_read_length:7;
		unsigned int const_urb_entry_read_offset:6;
		unsigned int pad1:1;
		unsigned int const_urb_entry_read_length:6;
		unsigned int pad2:1;
	} wm3;

	struct {
		unsigned int stats_enable:1;
		unsigned int pad0:1;
		unsigned int sampler_count:3;
		unsigned int sampler_state_pointer:27;
	} wm4;

	struct {
		unsigned int enable_8_pix:1;
		unsigned int enable_16_pix:1;
		unsigned int enable_32_pix:1;
		unsigned int enable_cont_32_pix:1; /* ctg+ */
		unsigned int enable_cont_64_pix:1; /* ctg+ */
		unsigned int pad0:1;
		unsigned int fast_span_coverage:1; /* ilk */
		unsigned int depth_clear:1; /* ilk */
		unsigned int depth_resolve:1; /* ilk */
		unsigned int hier_depth_resolve:1; /* ilk */
		unsigned int legacy_global_depth_bias:1;
		unsigned int line_stipple:1;
		unsigned int depth_offset:1;
		unsigned int polygon_stipple:1;
		unsigned int line_aa_region_width:2;
		unsigned int line_endcap_aa_region_width:2;
		unsigned int early_depth_test:1;
		unsigned int thread_dispatch_enable:1;
		unsigned int program_uses_depth:1;
		unsigned int program_computes_dpeth:1;
		unsigned int program_uses_killpixel:1;
		unsigned int legacy_line_rast:1;
		unsigned int transposed_urb_read:1;
		unsigned int max_threads:7;
	} wm5;

	struct {
		float global_depth_offset_constant;
	} wm6;

	struct {
		float global_depth_offset_scale;
	} wm7;

	/* ilk only from now on */
	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count_1:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer_1:26;
	} wm8;

	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count_2:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer_2:26;
	} wm9;

	struct {
		unsigned int pad0:1;
		unsigned int grf_reg_count_3:3;
		unsigned int pad1:2;
		unsigned int kernel_start_pointer_3:26;
	} wm10;
};

struct gen4_color_calc_state {
	struct {
		unsigned int pad0:3;
		unsigned int bf_stencil_pass_depth_pass_op:3;
		unsigned int bf_stencil_pass_depth_fail_op:3;
		unsigned int bf_stencil_fail_op:3;
		unsigned int bf_stencil_func:3;
		unsigned int bf_stencil_enable:1;
		unsigned int pad1:2;
		unsigned int stencil_write_enable:1;
		unsigned int stencil_pass_depth_pass_op:3;
		unsigned int stencil_pass_depth_fail_op:3;
		unsigned int stencil_fail_op:3;
		unsigned int stencil_func:3;
		unsigned int stencil_enable:1;
	} cc0;

	struct {
		unsigned int bf_stencil_ref:8;
		unsigned int stencil_write_mask:8;
		unsigned int stencil_test_mask:8;
		unsigned int stencil_ref:8;
	} cc1;

	struct {
		unsigned int logicop_enable:1;
		unsigned int pad0:10;
		unsigned int depth_write_enable:1;
		unsigned int depth_test_function:3;
		unsigned int depth_test:1;
		unsigned int bf_stencil_write_mask:8;
		unsigned int bf_stencil_test_mask:8;
	} cc2;

	struct {
		unsigned int pad0:8;
		unsigned int alpha_test_func:3;
		unsigned int alpha_test:1;
		unsigned int blend_enable:1;
		unsigned int ia_blend_enable:1;
		unsigned int pad1:1;
		unsigned int alpha_test_format:1;
		unsigned int pad2:16;
	} cc3;

	struct {
		unsigned int pad0:5;
		unsigned int cc_viewport_state_offset:27;
	} cc4;

	struct {
		unsigned int pad0:2;
		unsigned int ia_dest_blend_factor:5;
		unsigned int ia_src_blend_factor:5;
		unsigned int ia_blend_function:3;
		unsigned int stats_enable:1;
		unsigned int logicop_func:4;
		unsigned int pad1:10;
		unsigned int round_disable:1;
		unsigned int dither_enable:1;
	} cc5;

	struct {
		unsigned int clamp_post_alpha_blend:1;
		unsigned int clamp_pre_alpha_blend:1;
		unsigned int clamp_range:2;
		unsigned int pad0:11;
		unsigned int y_dither_offset:2;
		unsigned int x_dither_offset:2;
		unsigned int dest_blend_factor:5;
		unsigned int src_blend_factor:5;
		unsigned int blend_function:3;
	} cc6;

	struct {
		union {
			float f;
			unsigned char ub[4];
		} alpha_ref;
	} cc7;
};

#endif
