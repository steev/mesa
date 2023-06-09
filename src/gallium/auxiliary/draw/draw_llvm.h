/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef DRAW_LLVM_H
#define DRAW_LLVM_H

#include "draw/draw_private.h"

#include "draw/draw_vs.h"
#include "draw/draw_gs.h"
#include "draw/draw_tess.h"

#include "gallivm/lp_bld_sample.h"
#include "gallivm/lp_bld_limits.h"
#include "gallivm/lp_bld_jit_types.h"

#include "pipe/p_context.h"
#include "util/list.h"


struct draw_llvm;
struct llvm_vertex_shader;
struct llvm_geometry_shader;
struct llvm_tess_ctrl_shader;
struct llvm_tess_eval_shader;

struct draw_sampler_static_state
{
   /*
    * These attributes are effectively interleaved for more sane key handling.
    * However, there might be lots of null space if the amount of samplers and
    * textures isn't the same.
    */
   struct lp_static_sampler_state sampler_state;
   struct lp_static_texture_state texture_state;
};

struct draw_image_static_state
{
   struct lp_static_texture_state image_state;
};

enum {
   DRAW_JIT_VERTEX_VERTEX_ID = 0,
   DRAW_JIT_VERTEX_CLIP_POS,
   DRAW_JIT_VERTEX_DATA
};

/**
 * This structure is passed directly to the generated vertex shader.
 *
 * It contains the derived state.
 *
 * Changes here must be reflected in the draw_jit_context_* macros.
 * Changes to the ordering should be avoided.
 *
 * Only use types with a clear size and padding here, in particular prefer the
 * stdint.h types to the basic integer types.
 */
struct draw_jit_context
{
   struct lp_jit_buffer constants[LP_MAX_TGSI_CONST_BUFFERS];
   float (*planes) [DRAW_TOTAL_CLIP_PLANES][4];
   struct pipe_viewport_state *viewports;

   struct lp_jit_texture textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct lp_jit_sampler samplers[PIPE_MAX_SAMPLERS];
   struct lp_jit_image images[PIPE_MAX_SHADER_IMAGES];

   struct lp_jit_buffer ssbos[LP_MAX_TGSI_SHADER_BUFFERS];

   const float *aniso_filter_table;
};

enum {
   DRAW_JIT_CTX_CONSTANTS            = 0,
   DRAW_JIT_CTX_PLANES               = 1,
   DRAW_JIT_CTX_VIEWPORT             = 2,
   DRAW_JIT_CTX_TEXTURES             = 3,
   DRAW_JIT_CTX_SAMPLERS             = 4,
   DRAW_JIT_CTX_IMAGES               = 5,
   DRAW_JIT_CTX_SSBOS                = 6,
   DRAW_JIT_CTX_ANISO_FILTER_TABLE   = 7,
   DRAW_JIT_CTX_NUM_FIELDS
};

#define draw_jit_context_constants(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_JIT_CTX_CONSTANTS, "constants")

#define draw_jit_context_planes(_gallivm, _type, _ptr) \
   lp_build_struct_get2(_gallivm, _type, _ptr, DRAW_JIT_CTX_PLANES, "planes")

#define draw_jit_context_viewports(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_JIT_CTX_VIEWPORT, "viewports")

#define draw_jit_context_ssbos(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_JIT_CTX_SSBOS, "ssbos")

#define draw_jit_context_aniso_filter_table(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_JIT_CTX_ANISO_FILTER_TABLE, "aniso_filter_table")


#define draw_jit_header_id(_gallivm, _type, _ptr)              \
   lp_build_struct_get_ptr2(_gallivm, _type, _ptr, DRAW_JIT_VERTEX_VERTEX_ID, "id")

#define draw_jit_header_clip_pos(_gallivm, _type, _ptr) \
   lp_build_struct_get_ptr2(_gallivm, _type, _ptr, DRAW_JIT_VERTEX_CLIP_POS, "clip_pos")

#define draw_jit_header_data(_gallivm, _type, _ptr)            \
   lp_build_struct_get_ptr2(_gallivm, _type, _ptr, DRAW_JIT_VERTEX_DATA, "data")


#define draw_jit_vbuffer_stride(_gallivm, _type, _ptr)         \
   lp_build_struct_get2(_gallivm, _type, _ptr, 0, "stride")

#define draw_jit_vbuffer_offset(_gallivm, _type, _ptr)         \
   lp_build_struct_get2(_gallivm, _type, _ptr, 2, "buffer_offset")

enum {
   DRAW_JIT_DVBUFFER_MAP = 0,
   DRAW_JIT_DVBUFFER_SIZE,
   DRAW_JIT_DVBUFFER_NUM_FIELDS  /* number of fields above */
};

#define draw_jit_dvbuffer_map(_gallivm, _type, _ptr)         \
   lp_build_struct_get2(_gallivm, _type, _ptr, DRAW_JIT_DVBUFFER_MAP, "map")

#define draw_jit_dvbuffer_size(_gallivm, _type, _ptr)        \
   lp_build_struct_get2(_gallivm, _type, _ptr, DRAW_JIT_DVBUFFER_SIZE, "size")


/**
 * This structure is passed directly to the generated geometry shader.
 *
 * It contains the derived state.
 *
 * Changes here must be reflected in the draw_gs_jit_context_* macros.
 * Changes to the ordering should be avoided.
 *
 * Only use types with a clear size and padding here, in particular prefer the
 * stdint.h types to the basic integer types.
 */
struct draw_gs_jit_context
{
   struct lp_jit_buffer constants[LP_MAX_TGSI_CONST_BUFFERS];
   float (*planes) [DRAW_TOTAL_CLIP_PLANES][4];
   struct pipe_viewport_state *viewports;

   /* There two need to be exactly at DRAW_JIT_CTX_TEXTURES and
    * DRAW_JIT_CTX_SAMPLERS positions in the struct */
   struct lp_jit_texture textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct lp_jit_sampler samplers[PIPE_MAX_SAMPLERS];
   struct lp_jit_image images[PIPE_MAX_SHADER_IMAGES];

   int **prim_lengths;
   int *emitted_vertices;
   int *emitted_prims;
   struct lp_jit_buffer ssbos[LP_MAX_TGSI_SHADER_BUFFERS];

   const float *aniso_filter_table;
};

enum {
   DRAW_GS_JIT_CTX_CONSTANTS = 0,
   DRAW_GS_JIT_CTX_PLANES = 1,
   DRAW_GS_JIT_CTX_VIEWPORT = 2,
   /* Textures and samples are reserved for DRAW_JIT_CTX_TEXTURES
    * and DRAW_JIT_CTX_SAMPLERS, because they both need
    * to be at exactly the same locations as they are in the
    * VS ctx structure for sampling to work. */
   DRAW_GS_JIT_CTX_TEXTURES = DRAW_JIT_CTX_TEXTURES,
   DRAW_GS_JIT_CTX_SAMPLERS = DRAW_JIT_CTX_SAMPLERS,
   DRAW_GS_JIT_CTX_IMAGES = DRAW_JIT_CTX_IMAGES,
   DRAW_GS_JIT_CTX_PRIM_LENGTHS = 6,
   DRAW_GS_JIT_CTX_EMITTED_VERTICES = 7,
   DRAW_GS_JIT_CTX_EMITTED_PRIMS = 8,
   DRAW_GS_JIT_CTX_SSBOS = 9,
   DRAW_GS_JIT_CTX_ANISO_FILTER_TABLE = 10,
   DRAW_GS_JIT_CTX_NUM_FIELDS = 11
};

#define draw_gs_jit_context_constants(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_CONSTANTS, "constants")

#define draw_gs_jit_prim_lengths(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_PRIM_LENGTHS, "prim_lengths")

#define draw_gs_jit_emitted_vertices(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_EMITTED_VERTICES, "emitted_vertices")

#define draw_gs_jit_emitted_prims(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_EMITTED_PRIMS, "emitted_prims")

#define draw_gs_jit_context_ssbos(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_SSBOS, "ssbos")

#define draw_gs_jit_context_aniso_filter_table(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_GS_JIT_CTX_ANISO_FILTER_TABLE, "aniso_filter_table")

struct draw_tcs_jit_context {
   struct lp_jit_buffer constants[LP_MAX_TGSI_CONST_BUFFERS];

   int dummy1;
   int dummy2;
   /* There two need to be exactly at DRAW_JIT_CTX_TEXTURES and
    * DRAW_JIT_CTX_SAMPLERS positions in the struct */
   struct lp_jit_texture textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct lp_jit_sampler samplers[PIPE_MAX_SAMPLERS];
   struct lp_jit_image images[PIPE_MAX_SHADER_IMAGES];

   struct lp_jit_buffer ssbos[LP_MAX_TGSI_SHADER_BUFFERS];

   const float *aniso_filter_table;
};

enum {
   DRAW_TCS_JIT_CTX_CONSTANTS = 0,
   DRAW_TCS_JIT_CTX_DUMMY1 = 1,
   DRAW_TCS_JIT_CTX_DUMMY2 = 2,
   DRAW_TCS_JIT_CTX_TEXTURES = DRAW_JIT_CTX_TEXTURES,
   DRAW_TCS_JIT_CTX_SAMPLERS = DRAW_JIT_CTX_SAMPLERS,
   DRAW_TCS_JIT_CTX_IMAGES = DRAW_JIT_CTX_IMAGES,
   DRAW_TCS_JIT_CTX_SSBOS = 6,
   DRAW_TCS_JIT_CTX_ANISO_FILTER_TABLE = 7,
   DRAW_TCS_JIT_CTX_NUM_FIELDS = 8,
};

#define draw_tcs_jit_context_constants(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TCS_JIT_CTX_CONSTANTS, "constants")

#define draw_tcs_jit_context_ssbos(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TCS_JIT_CTX_SSBOS, "ssbos")

#define draw_tcs_jit_context_aniso_filter_table(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TCS_JIT_CTX_ANISO_FILTER_TABLE, "aniso_filter_table")

struct draw_tes_jit_context {
   struct lp_jit_buffer constants[LP_MAX_TGSI_CONST_BUFFERS];

   int dummy1;
   int dummy2;
   /* There two need to be exactly at DRAW_JIT_CTX_TEXTURES and
    * DRAW_JIT_CTX_SAMPLERS positions in the struct */
   struct lp_jit_texture textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct lp_jit_sampler samplers[PIPE_MAX_SAMPLERS];
   struct lp_jit_image images[PIPE_MAX_SHADER_IMAGES];

   struct lp_jit_buffer ssbos[LP_MAX_TGSI_SHADER_BUFFERS];

   const float *aniso_filter_table;
};

enum {
   DRAW_TES_JIT_CTX_CONSTANTS = 0,
   DRAW_TES_JIT_CTX_DUMMY1 = 1,
   DRAW_TES_JIT_CTX_DUMMY2 = 2,
   DRAW_TES_JIT_CTX_TEXTURES = DRAW_JIT_CTX_TEXTURES,
   DRAW_TES_JIT_CTX_SAMPLERS = DRAW_JIT_CTX_SAMPLERS,
   DRAW_TES_JIT_CTX_IMAGES = DRAW_JIT_CTX_IMAGES,
   DRAW_TES_JIT_CTX_SSBOS = 6,
   DRAW_TES_JIT_CTX_ANISO_FILTER_TABLE = 7,
   DRAW_TES_JIT_CTX_NUM_FIELDS = 8,
};

#define draw_tes_jit_context_constants(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TES_JIT_CTX_CONSTANTS, "constants")

#define draw_tes_jit_context_ssbos(_variant, _ptr) \
   lp_build_struct_get_ptr2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TES_JIT_CTX_SSBOS, "ssbos")

#define draw_tes_jit_context_aniso_filter_table(_variant, _ptr) \
   lp_build_struct_get2(_variant->gallivm, _variant->context_type, _ptr, DRAW_TES_JIT_CTX_ANISO_FILTER_TABLE, "aniso_filter_table")

typedef boolean
(*draw_jit_vert_func)(struct draw_jit_context *context,
                      struct vertex_header *io,
                      const struct draw_vertex_buffer vbuffers[PIPE_MAX_ATTRIBS],
                      unsigned count,
                      unsigned start,
                      unsigned stride,
                      struct pipe_vertex_buffer *vertex_buffers,
                      unsigned instance_id,
                      unsigned vertex_id_offset,
                      unsigned start_instance,
                      const unsigned *fetch_elts,
                      unsigned draw_id, unsigned view_id);


typedef int
(*draw_gs_jit_func)(struct draw_gs_jit_context *context,
                    float inputs[6][PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS][TGSI_NUM_CHANNELS],
                    struct vertex_header **output,
                    unsigned num_prims,
                    unsigned instance_id,
                    int *prim_ids,
                    unsigned invocation_id,
                    unsigned view_id);

typedef int
(*draw_tcs_jit_func)(struct draw_tcs_jit_context *context,
                     float inputs[32][NUM_TCS_INPUTS][TGSI_NUM_CHANNELS],
                     float outputs[32][PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS],
                     uint32_t prim_id, uint32_t patch_vertices_in,
                     unsigned view_id);

typedef int
(*draw_tes_jit_func)(struct draw_tes_jit_context *context,
                     float inputs[32][PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS],
                     struct vertex_header *io,
                     uint32_t prim_id, uint32_t num_tess_coord,
                     float *tess_coord_x, float *tess_coord_y, float *tess_outer,
                     float *tess_inner, uint32_t patch_vertices_in,
                     unsigned view_id);


struct draw_llvm_variant_key
{
   unsigned nr_vertex_elements:8;
   unsigned nr_samplers:8;
   unsigned nr_sampler_views:8;
   unsigned nr_images:8;
   unsigned clamp_vertex_color:1;
   unsigned clip_xy:1;
   unsigned clip_z:1;
   unsigned clip_user:1;
   unsigned clip_halfz:1;
   unsigned bypass_viewport:1;
   unsigned need_edgeflags:1;
   unsigned has_gs_or_tes:1;
   unsigned num_outputs:8;
   unsigned ucp_enable:PIPE_MAX_CLIP_PLANES;
   /* note padding here - must use memset */

   /* Variable number of vertex elements:
    */
   struct pipe_vertex_element vertex_element[1];

   /* Followed by variable number of samplers:
    */
/*   struct draw_sampler_static_state sampler; */
   /* Followed by variable number of images
    */
};

struct draw_gs_llvm_variant_key
{
   unsigned nr_samplers:8;
   unsigned nr_sampler_views:8;
   unsigned nr_images:8;
   unsigned num_outputs:8;
   /* note padding here - must use memset */
   unsigned clamp_vertex_color:1;
   struct draw_sampler_static_state samplers[1];
   /* Followed by variable number of images.*/
};

struct draw_tcs_llvm_variant_key
{
   unsigned nr_samplers:8;
   unsigned nr_sampler_views:8;
   unsigned nr_images:8;
   struct draw_sampler_static_state samplers[1];
   /* Followed by variable number of images.*/
};

struct draw_tes_llvm_variant_key
{
   unsigned nr_samplers:8;
   unsigned nr_sampler_views:8;
   unsigned nr_images:8;
   unsigned primid_output:7;
   unsigned primid_needed:1;
   unsigned clamp_vertex_color:1;
   struct draw_sampler_static_state samplers[1];
   /* Followed by variable number of images.*/
};

#define DRAW_LLVM_MAX_VARIANT_KEY_SIZE \
   (sizeof(struct draw_llvm_variant_key) +	\
    PIPE_MAX_SHADER_SAMPLER_VIEWS * sizeof(struct draw_sampler_static_state) +	\
    PIPE_MAX_SHADER_IMAGES * sizeof(struct draw_image_static_state) + \
    (PIPE_MAX_ATTRIBS-1) * sizeof(struct pipe_vertex_element))

#define DRAW_GS_LLVM_MAX_VARIANT_KEY_SIZE \
   (sizeof(struct draw_gs_llvm_variant_key) +	\
    PIPE_MAX_SHADER_IMAGES * sizeof(struct draw_image_static_state) + \
    PIPE_MAX_SHADER_SAMPLER_VIEWS * sizeof(struct draw_sampler_static_state))

#define DRAW_TCS_LLVM_MAX_VARIANT_KEY_SIZE \
   (sizeof(struct draw_tcs_llvm_variant_key) +	\
    PIPE_MAX_SHADER_IMAGES * sizeof(struct draw_image_static_state) + \
    PIPE_MAX_SHADER_SAMPLER_VIEWS * sizeof(struct draw_sampler_static_state))

#define DRAW_TES_LLVM_MAX_VARIANT_KEY_SIZE \
   (sizeof(struct draw_tes_llvm_variant_key) +	\
    PIPE_MAX_SHADER_IMAGES * sizeof(struct draw_image_static_state) + \
    PIPE_MAX_SHADER_SAMPLER_VIEWS * sizeof(struct draw_sampler_static_state))


static inline size_t
draw_llvm_variant_key_size(unsigned nr_vertex_elements,
                           unsigned nr_samplers,
                           unsigned nr_sampler_views,
                           unsigned nr_images)
{
   return (sizeof(struct draw_llvm_variant_key) +
           (nr_vertex_elements - 1) * sizeof(struct pipe_vertex_element) +
           MAX2(nr_samplers, nr_sampler_views) *
               sizeof(struct draw_sampler_static_state) +
           nr_images * sizeof(struct draw_image_static_state));
}


static inline size_t
draw_gs_llvm_variant_key_size(unsigned nr_samplers,
                              unsigned nr_sampler_views,
                              unsigned nr_images)
{
   return (sizeof(struct draw_gs_llvm_variant_key) +
           (MAX2(nr_samplers, nr_sampler_views) - 1) *
               sizeof(struct draw_sampler_static_state) +
           nr_images * sizeof(struct draw_sampler_static_state));
}

static inline size_t
draw_tcs_llvm_variant_key_size(unsigned nr_samplers,
                               unsigned nr_sampler_views,
                               unsigned nr_images)
{
   return (sizeof(struct draw_tcs_llvm_variant_key) +
           (MAX2(nr_samplers, nr_sampler_views) - 1) *
               sizeof(struct draw_sampler_static_state) +
           nr_images * sizeof(struct draw_sampler_static_state));
}

static inline size_t
draw_tes_llvm_variant_key_size(unsigned nr_samplers,
                               unsigned nr_sampler_views,
                               unsigned nr_images)
{
   return (sizeof(struct draw_tes_llvm_variant_key) +
           (MAX2(nr_samplers, nr_sampler_views) - 1) *
               sizeof(struct draw_sampler_static_state) +
           nr_images * sizeof(struct draw_sampler_static_state));
}

static inline struct draw_sampler_static_state *
draw_llvm_variant_key_samplers(struct draw_llvm_variant_key *key)
{
   return (struct draw_sampler_static_state *)
      &key->vertex_element[key->nr_vertex_elements];
}

static inline struct draw_image_static_state *
draw_llvm_variant_key_images(struct draw_llvm_variant_key *key)
{
   struct draw_sampler_static_state *samplers = (struct draw_sampler_static_state *)
      (&key->vertex_element[key->nr_vertex_elements]);
   return (struct draw_image_static_state *)
      &samplers[MAX2(key->nr_samplers, key->nr_sampler_views)];
}

static inline struct draw_image_static_state *
draw_gs_llvm_variant_key_images(struct draw_gs_llvm_variant_key *key)
{
   return (struct draw_image_static_state *)
      &key->samplers[MAX2(key->nr_samplers, key->nr_sampler_views)];
}

static inline struct draw_image_static_state *
draw_tcs_llvm_variant_key_images(struct draw_tcs_llvm_variant_key *key)
{
   return (struct draw_image_static_state *)
      &key->samplers[MAX2(key->nr_samplers, key->nr_sampler_views)];
}

static inline struct draw_image_static_state *
draw_tes_llvm_variant_key_images(struct draw_tes_llvm_variant_key *key)
{
   return (struct draw_image_static_state *)
      &key->samplers[MAX2(key->nr_samplers, key->nr_sampler_views)];
}

struct draw_llvm_variant_list_item
{
   struct list_head list;
   struct draw_llvm_variant *base;
};

struct draw_gs_llvm_variant_list_item
{
   struct list_head list;
   struct draw_gs_llvm_variant *base;
};

struct draw_tcs_llvm_variant_list_item
{
   struct list_head list;
   struct draw_tcs_llvm_variant *base;
};

struct draw_tes_llvm_variant_list_item
{
   struct list_head list;
   struct draw_tes_llvm_variant *base;
};

struct draw_llvm_variant
{
   struct gallivm_state *gallivm;

   /* LLVM JIT builder types */
   LLVMTypeRef context_type;
   LLVMTypeRef context_ptr_type;

   LLVMTypeRef buffer_type;
   LLVMTypeRef buffer_ptr_type;

   LLVMTypeRef vb_type;
   LLVMTypeRef vb_ptr_type;

   LLVMTypeRef vertex_header_type;
   LLVMTypeRef vertex_header_ptr_type;

   LLVMValueRef function;
   draw_jit_vert_func jit_func;

   struct llvm_vertex_shader *shader;

   struct draw_llvm *llvm;
   struct draw_llvm_variant_list_item list_item_global;
   struct draw_llvm_variant_list_item list_item_local;

   /* key is variable-sized, must be last */
   struct draw_llvm_variant_key key;
};


struct draw_gs_llvm_variant
{
   struct gallivm_state *gallivm;

   /* LLVM JIT builder types */
   LLVMTypeRef context_type;
   LLVMTypeRef context_ptr_type;

   LLVMTypeRef vertex_header_type;
   LLVMTypeRef vertex_header_ptr_type;

   LLVMTypeRef input_array_type;

   LLVMValueRef context_ptr;
   LLVMValueRef io_ptr;
   LLVMValueRef num_prims;
   LLVMValueRef function;
   draw_gs_jit_func jit_func;

   struct llvm_geometry_shader *shader;

   struct draw_llvm *llvm;
   struct draw_gs_llvm_variant_list_item list_item_global;
   struct draw_gs_llvm_variant_list_item list_item_local;

   /* key is variable-sized, must be last */
   struct draw_gs_llvm_variant_key key;
};

struct draw_tcs_llvm_variant
{
   struct gallivm_state *gallivm;

   /* LLVM JIT builder types */
   LLVMTypeRef context_type;
   LLVMTypeRef context_ptr_type;
   LLVMTypeRef input_array_type;
   LLVMTypeRef output_array_type;

   LLVMValueRef context_ptr;
   /* LLVMValueRef io_ptr; */
   LLVMValueRef num_prims;
   LLVMValueRef function;
   draw_tcs_jit_func jit_func;

   struct llvm_tess_ctrl_shader *shader;

   struct draw_llvm *llvm;
   struct draw_tcs_llvm_variant_list_item list_item_global;
   struct draw_tcs_llvm_variant_list_item list_item_local;

   /* key is variable-sized, must be last */
   struct draw_tcs_llvm_variant_key key;
};

struct draw_tes_llvm_variant
{
   struct gallivm_state *gallivm;

   /* LLVM JIT builder types */
   LLVMTypeRef context_type;
   LLVMTypeRef context_ptr_type;
   LLVMTypeRef vertex_header_ptr_type;
   LLVMTypeRef input_array_type;
   LLVMTypeRef patch_input_array_type;

   LLVMTypeRef input_array_deref_type;
   LLVMTypeRef vertex_header_type;

   LLVMValueRef context_ptr;
   LLVMValueRef io_ptr;
   LLVMValueRef num_prims;
   LLVMValueRef function;
   draw_tes_jit_func jit_func;

   struct llvm_tess_eval_shader *shader;

   struct draw_llvm *llvm;
   struct draw_tes_llvm_variant_list_item list_item_global;
   struct draw_tes_llvm_variant_list_item list_item_local;

   /* key is variable-sized, must be last */
   struct draw_tes_llvm_variant_key key;
};

struct llvm_vertex_shader {
   struct draw_vertex_shader base;

   unsigned variant_key_size;
   struct draw_llvm_variant_list_item variants;
   unsigned variants_created;
   unsigned variants_cached;
};

struct llvm_geometry_shader {
   struct draw_geometry_shader base;

   unsigned variant_key_size;
   struct draw_gs_llvm_variant_list_item variants;
   unsigned variants_created;
   unsigned variants_cached;
};

struct llvm_tess_ctrl_shader {
   struct draw_tess_ctrl_shader base;

   unsigned variant_key_size;
   struct draw_tcs_llvm_variant_list_item variants;
   unsigned variants_created;
   unsigned variants_cached;
};

struct llvm_tess_eval_shader {
   struct draw_tess_eval_shader base;

   unsigned variant_key_size;
   struct draw_tes_llvm_variant_list_item variants;
   unsigned variants_created;
   unsigned variants_cached;
};

struct draw_llvm {
   struct draw_context *draw;

   LLVMContextRef context;
   boolean context_owned;

   struct draw_jit_context jit_context;
   struct draw_gs_jit_context gs_jit_context;
   struct draw_tcs_jit_context tcs_jit_context;
   struct draw_tes_jit_context tes_jit_context;

   struct draw_llvm_variant_list_item vs_variants_list;
   int nr_variants;

   struct draw_gs_llvm_variant_list_item gs_variants_list;
   int nr_gs_variants;

   struct draw_tcs_llvm_variant_list_item tcs_variants_list;
   int nr_tcs_variants;

   struct draw_tes_llvm_variant_list_item tes_variants_list;
   int nr_tes_variants;
};


static inline struct llvm_vertex_shader *
llvm_vertex_shader(struct draw_vertex_shader *vs)
{
   return (struct llvm_vertex_shader *)vs;
}

static inline struct llvm_geometry_shader *
llvm_geometry_shader(struct draw_geometry_shader *gs)
{
   return (struct llvm_geometry_shader *)gs;
}

static inline struct llvm_tess_ctrl_shader *
llvm_tess_ctrl_shader(struct draw_tess_ctrl_shader *tcs)
{
   return (struct llvm_tess_ctrl_shader *)tcs;
}

static inline struct llvm_tess_eval_shader *
llvm_tess_eval_shader(struct draw_tess_eval_shader *tes)
{
   return (struct llvm_tess_eval_shader *)tes;
}

struct draw_llvm *
draw_llvm_create(struct draw_context *draw, LLVMContextRef llvm_context);

void
draw_llvm_destroy(struct draw_llvm *llvm);

struct draw_llvm_variant *
draw_llvm_create_variant(struct draw_llvm *llvm,
                         unsigned num_vertex_header_attribs,
                         const struct draw_llvm_variant_key *key);

void
draw_llvm_destroy_variant(struct draw_llvm_variant *variant);

struct draw_llvm_variant_key *
draw_llvm_make_variant_key(struct draw_llvm *llvm, char *store);

void
draw_llvm_dump_variant_key(struct draw_llvm_variant_key *key);


struct draw_gs_llvm_variant *
draw_gs_llvm_create_variant(struct draw_llvm *llvm,
                            unsigned num_vertex_header_attribs,
                            const struct draw_gs_llvm_variant_key *key);

void
draw_gs_llvm_destroy_variant(struct draw_gs_llvm_variant *variant);

struct draw_gs_llvm_variant_key *
draw_gs_llvm_make_variant_key(struct draw_llvm *llvm, char *store);

void
draw_gs_llvm_dump_variant_key(struct draw_gs_llvm_variant_key *key);

struct draw_tcs_llvm_variant *
draw_tcs_llvm_create_variant(struct draw_llvm *llvm,
                             unsigned num_vertex_header_attribs,
                             const struct draw_tcs_llvm_variant_key *key);

void
draw_tcs_llvm_destroy_variant(struct draw_tcs_llvm_variant *variant);

struct draw_tcs_llvm_variant_key *
draw_tcs_llvm_make_variant_key(struct draw_llvm *llvm, char *store);

void
draw_tcs_llvm_dump_variant_key(struct draw_tcs_llvm_variant_key *key);

struct draw_tes_llvm_variant *
draw_tes_llvm_create_variant(struct draw_llvm *llvm,
                             unsigned num_vertex_header_attribs,
                             const struct draw_tes_llvm_variant_key *key);

void
draw_tes_llvm_destroy_variant(struct draw_tes_llvm_variant *variant);

struct draw_tes_llvm_variant_key *
draw_tes_llvm_make_variant_key(struct draw_llvm *llvm, char *store);

void
draw_tes_llvm_dump_variant_key(struct draw_tes_llvm_variant_key *key);

struct lp_build_sampler_soa *
draw_llvm_sampler_soa_create(const struct draw_sampler_static_state *static_state,
                             unsigned nr_samplers);
static inline void
draw_llvm_sampler_soa_destroy(struct lp_build_sampler_soa *sampler)
{
   FREE(sampler);
}

struct lp_build_image_soa *
draw_llvm_image_soa_create(const struct draw_image_static_state *static_state,
                           unsigned nr_images);

static inline void
draw_llvm_image_soa_destroy(struct lp_build_image_soa *image)
{
   FREE(image);
}

void
draw_llvm_set_sampler_state(struct draw_context *draw,
                            enum pipe_shader_type shader_stage);

void
draw_llvm_set_mapped_texture(struct draw_context *draw,
                             enum pipe_shader_type shader_stage,
                             unsigned sview_idx,
                             uint32_t width, uint32_t height, uint32_t depth,
                             uint32_t first_level, uint32_t last_level,
                             uint32_t num_samples,
                             uint32_t sample_stride,
                             const void *base_ptr,
                             uint32_t row_stride[PIPE_MAX_TEXTURE_LEVELS],
                             uint32_t img_stride[PIPE_MAX_TEXTURE_LEVELS],
                             uint32_t mip_offsets[PIPE_MAX_TEXTURE_LEVELS]);

void
draw_llvm_set_mapped_image(struct draw_context *draw,
                           enum pipe_shader_type shader_stage,
                           unsigned idx,
                           uint32_t width, uint32_t height, uint32_t depth,
                           const void *base_ptr,
                           uint32_t row_stride,
                           uint32_t img_stride,
                           uint32_t num_samples,
                           uint32_t sample_stride);
#endif
