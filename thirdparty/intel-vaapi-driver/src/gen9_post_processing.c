/*
 * Copyright © 2014 Intel Corporation
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
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_driver.h"
#include "i965_defines.h"
#include "i965_structs.h"
#include "i965_drv_video.h"
#include "i965_post_processing.h"
#include "i965_render.h"
#include "intel_media.h"

#include "i965_yuv_coefs.h"
#include "gen8_post_processing.h"
#include "gen75_picture_process.h"
#include "intel_gen_vppapi.h"
#include "intel_common_vpp_internal.h"

static const uint32_t pp_null_gen9[][4] = {
};

static const uint32_t pp_nv12_load_save_nv12_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_pl2.g9b"
};

static const uint32_t pp_nv12_load_save_pl3_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_pl3.g9b"
};

static const uint32_t pp_pl3_load_save_nv12_gen9[][4] = {
#include "shaders/post_processing/gen9/pl3_to_pl2.g9b"
};

static const uint32_t pp_pl3_load_save_pl3_gen9[][4] = {
#include "shaders/post_processing/gen9/pl3_to_pl3.g9b"
};

static const uint32_t pp_nv12_scaling_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_pl2.g9b"
};

static const uint32_t pp_nv12_avs_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_pl2.g9b"
};

static const uint32_t pp_nv12_dndi_gen9[][4] = {
};

static const uint32_t pp_nv12_dn_gen9[][4] = {
};

static const uint32_t pp_nv12_load_save_pa_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_pa.g9b"
};

static const uint32_t pp_pl3_load_save_pa_gen9[][4] = {
#include "shaders/post_processing/gen9/pl3_to_pa.g9b"
};

static const uint32_t pp_pa_load_save_nv12_gen9[][4] = {
#include "shaders/post_processing/gen9/pa_to_pl2.g9b"
};

static const uint32_t pp_pa_load_save_pl3_gen9[][4] = {
#include "shaders/post_processing/gen9/pa_to_pl3.g9b"
};

static const uint32_t pp_pa_load_save_pa_gen9[][4] = {
#include "shaders/post_processing/gen9/pa_to_pa.g9b"
};

static const uint32_t pp_rgbx_load_save_nv12_gen9[][4] = {
#include "shaders/post_processing/gen9/rgbx_to_nv12.g9b"
};

static const uint32_t pp_nv12_load_save_rgbx_gen9[][4] = {
#include "shaders/post_processing/gen9/pl2_to_rgbx.g9b"
};

#define MAX_SCALING_SURFACES    16

#define DEFAULT_MOCS    0x02

static const uint32_t pp_10bit_scaling_gen9[][4] = {
#include "shaders/post_processing/gen9/conv_p010.g9b"
};

static const uint32_t pp_yuv420p8_scaling_gen9[][4] = {
#include "shaders/post_processing/gen9/conv_nv12.g9b"
};

static const uint32_t pp_10bit_8bit_scaling_gen9[][4] = {
#include "shaders/post_processing/gen9/conv_10bit_8bit.g9b"
};

static const uint32_t pp_8bit_420_rgb32_scaling_gen9[][4] = {
#include "shaders/post_processing/gen9/conv_8bit_420_rgb32.g9b"
};

static const uint32_t pp_clear_yuy2_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_yuy2.g9b"
};

static const uint32_t pp_clear_uyvy_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_uyvy.g9b"
};

static const uint32_t pp_clear_pl2_8bit_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_pl2_8bit.g9b"
};

static const uint32_t pp_clear_pl3_8bit_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_pl3_8bit.g9b"
};

static const uint32_t pp_clear_rgbx_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_rgbx.g9b"
};

static const uint32_t pp_clear_bgrx_gen9[][4] = {
#include "shaders/post_processing/gen9/clear_bgrx.g9b"
};

struct i965_kernel pp_common_scaling_gen9[] = {
    {
        "10bit to 10bit",
        0,
        pp_10bit_scaling_gen9,
        sizeof(pp_10bit_scaling_gen9),
        NULL,
    },

    {
        "8bit to 8bit",
        1,
        pp_yuv420p8_scaling_gen9,
        sizeof(pp_yuv420p8_scaling_gen9),
        NULL,
    },

    {
        "10bit to 8bit",
        2,
        pp_10bit_8bit_scaling_gen9,
        sizeof(pp_10bit_8bit_scaling_gen9),
        NULL,
    },

    {
        "8bit 420 to rgb32",
        3,
        pp_8bit_420_rgb32_scaling_gen9,
        sizeof(pp_8bit_420_rgb32_scaling_gen9),
        NULL,
    },
};

struct i965_kernel pp_clear_gen9[] = {
    {
        "pl2 8bit",
        0,
        pp_clear_pl2_8bit_gen9,
        sizeof(pp_clear_pl2_8bit_gen9),
        NULL,
    },

    {
        "pl3 8bit",
        1,
        pp_clear_pl3_8bit_gen9,
        sizeof(pp_clear_pl3_8bit_gen9),
        NULL,
    },

    {
        "yuy2",
        2,
        pp_clear_yuy2_gen9,
        sizeof(pp_clear_yuy2_gen9),
        NULL,
    },

    {
        "uyvy",
        3,
        pp_clear_uyvy_gen9,
        sizeof(pp_clear_uyvy_gen9),
        NULL,
    },

    {
        "rgbx",
        4,
        pp_clear_rgbx_gen9,
        sizeof(pp_clear_rgbx_gen9),
        NULL,
    },

    {
        "bgrx",
        5,
        pp_clear_bgrx_gen9,
        sizeof(pp_clear_bgrx_gen9),
        NULL,
    },
};

static struct pp_module pp_modules_gen9[] = {
    {
        {
            "NULL module (for testing)",
            PP_NULL,
            pp_null_gen9,
            sizeof(pp_null_gen9),
            NULL,
        },

        pp_null_initialize,
    },

    {
        {
            "NV12_NV12",
            PP_NV12_LOAD_SAVE_N12,
            pp_nv12_load_save_nv12_gen9,
            sizeof(pp_nv12_load_save_nv12_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "NV12_PL3",
            PP_NV12_LOAD_SAVE_PL3,
            pp_nv12_load_save_pl3_gen9,
            sizeof(pp_nv12_load_save_pl3_gen9),
            NULL,
        },
        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PL3_NV12",
            PP_PL3_LOAD_SAVE_N12,
            pp_pl3_load_save_nv12_gen9,
            sizeof(pp_pl3_load_save_nv12_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PL3_PL3",
            PP_PL3_LOAD_SAVE_PL3,
            pp_pl3_load_save_pl3_gen9,
            sizeof(pp_pl3_load_save_pl3_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "NV12 Scaling module",
            PP_NV12_SCALING,
            pp_nv12_scaling_gen9,
            sizeof(pp_nv12_scaling_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "NV12 AVS module",
            PP_NV12_AVS,
            pp_nv12_avs_gen9,
            sizeof(pp_nv12_avs_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "NV12 DNDI module",
            PP_NV12_DNDI,
            pp_nv12_dndi_gen9,
            sizeof(pp_nv12_dndi_gen9),
            NULL,
        },

        pp_null_initialize,
    },

    {
        {
            "NV12 DN module",
            PP_NV12_DN,
            pp_nv12_dn_gen9,
            sizeof(pp_nv12_dn_gen9),
            NULL,
        },

        pp_null_initialize,
    },
    {
        {
            "NV12_PA module",
            PP_NV12_LOAD_SAVE_PA,
            pp_nv12_load_save_pa_gen9,
            sizeof(pp_nv12_load_save_pa_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PL3_PA module",
            PP_PL3_LOAD_SAVE_PA,
            pp_pl3_load_save_pa_gen9,
            sizeof(pp_pl3_load_save_pa_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PA_NV12 module",
            PP_PA_LOAD_SAVE_NV12,
            pp_pa_load_save_nv12_gen9,
            sizeof(pp_pa_load_save_nv12_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PA_PL3 module",
            PP_PA_LOAD_SAVE_PL3,
            pp_pa_load_save_pl3_gen9,
            sizeof(pp_pa_load_save_pl3_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "PA_PA module",
            PP_PA_LOAD_SAVE_PA,
            pp_pa_load_save_pa_gen9,
            sizeof(pp_pa_load_save_pa_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "RGBX_NV12 module",
            PP_RGBX_LOAD_SAVE_NV12,
            pp_rgbx_load_save_nv12_gen9,
            sizeof(pp_rgbx_load_save_nv12_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },

    {
        {
            "NV12_RGBX module",
            PP_NV12_LOAD_SAVE_RGBX,
            pp_nv12_load_save_rgbx_gen9,
            sizeof(pp_nv12_load_save_rgbx_gen9),
            NULL,
        },

        gen8_pp_plx_avs_initialize,
    },
};

static const AVSConfig gen9_avs_config = {
    .coeff_frac_bits = 6,
    .coeff_epsilon = 1.0f / (1U << 6),
    .num_phases = 31,
    .num_luma_coeffs = 8,
    .num_chroma_coeffs = 4,

    .coeff_range = {
        .lower_bound = {
            .y_k_h = { -2, -2, -2, -2, -2, -2, -2, -2 },
            .y_k_v = { -2, -2, -2, -2, -2, -2, -2, -2 },
            .uv_k_h = { -2, -2, -2, -2 },
            .uv_k_v = { -2, -2, -2, -2 },
        },
        .upper_bound = {
            .y_k_h = { 2, 2, 2, 2, 2, 2, 2, 2 },
            .y_k_v = { 2, 2, 2, 2, 2, 2, 2, 2 },
            .uv_k_h = { 2, 2, 2, 2 },
            .uv_k_v = { 2, 2, 2, 2 },
        },
    },
};

static void
gen9_pp_pipeline_select(VADriverContextP ctx,
                        struct i965_post_processing_context *pp_context)
{
    struct intel_batchbuffer *batch = pp_context->batch;

    BEGIN_BATCH(batch, 1);
    OUT_BATCH(batch,
              CMD_PIPELINE_SELECT |
              PIPELINE_SELECT_MEDIA |
              GEN9_FORCE_MEDIA_AWAKE_ON |
              GEN9_MEDIA_DOP_GATE_OFF |
              GEN9_PIPELINE_SELECTION_MASK |
              GEN9_MEDIA_DOP_GATE_MASK |
              GEN9_FORCE_MEDIA_AWAKE_MASK);
    ADVANCE_BATCH(batch);
}

static void
gen9_pp_state_base_address(VADriverContextP ctx,
                           struct i965_post_processing_context *pp_context)
{
    struct intel_batchbuffer *batch = pp_context->batch;

    BEGIN_BATCH(batch, 19);
    OUT_BATCH(batch, CMD_STATE_BASE_ADDRESS | (19 - 2));
    /* DW1 Generate state address */
    OUT_BATCH(batch, 0 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0);
    OUT_BATCH(batch, 0);
    /* DW4-5 Surface state address */
    OUT_RELOC64(batch, pp_context->surface_state_binding_table.bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY); /* Surface state base address */
    /* DW6-7 Dynamic state address */
    OUT_RELOC64(batch, pp_context->dynamic_state.bo, I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_SAMPLER,
                0, 0 | BASE_ADDRESS_MODIFY);

    /* DW8. Indirect object address */
    OUT_BATCH(batch, 0 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0);

    /* DW10-11 Instruction base address */
    OUT_RELOC64(batch, pp_context->instruction_state.bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);

    OUT_BATCH(batch, 0xFFFF0000 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0xFFFF0000 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0xFFFF0000 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0xFFFF0000 | BASE_ADDRESS_MODIFY);

    /* Bindless surface state base address */
    OUT_BATCH(batch, 0 | BASE_ADDRESS_MODIFY);
    OUT_BATCH(batch, 0);
    OUT_BATCH(batch, 0xfffff000);

    ADVANCE_BATCH(batch);
}

static void
gen9_pp_end_pipeline(VADriverContextP ctx,
                     struct i965_post_processing_context *pp_context)
{
    struct intel_batchbuffer *batch = pp_context->batch;

    BEGIN_BATCH(batch, 1);
    OUT_BATCH(batch,
              CMD_PIPELINE_SELECT |
              PIPELINE_SELECT_MEDIA |
              GEN9_FORCE_MEDIA_AWAKE_OFF |
              GEN9_MEDIA_DOP_GATE_ON |
              GEN9_PIPELINE_SELECTION_MASK |
              GEN9_MEDIA_DOP_GATE_MASK |
              GEN9_FORCE_MEDIA_AWAKE_MASK);
    ADVANCE_BATCH(batch);
}

static void
gen9_pp_pipeline_setup(VADriverContextP ctx,
                       struct i965_post_processing_context *pp_context)
{
    struct intel_batchbuffer *batch = pp_context->batch;

    intel_batchbuffer_start_atomic(batch, 0x1000);
    intel_batchbuffer_emit_mi_flush(batch);
    gen9_pp_pipeline_select(ctx, pp_context);
    gen9_pp_state_base_address(ctx, pp_context);
    gen8_pp_vfe_state(ctx, pp_context);
    gen8_pp_curbe_load(ctx, pp_context);
    gen8_interface_descriptor_load(ctx, pp_context);
    gen8_pp_object_walker(ctx, pp_context);
    gen9_pp_end_pipeline(ctx, pp_context);
    intel_batchbuffer_end_atomic(batch);
}

static VAStatus
gen9_post_processing(VADriverContextP ctx,
                     struct i965_post_processing_context *pp_context,
                     const struct i965_surface *src_surface,
                     const VARectangle *src_rect,
                     struct i965_surface *dst_surface,
                     const VARectangle *dst_rect,
                     int pp_index,
                     void * filter_param)
{
    VAStatus va_status;

    va_status = gen8_pp_initialize(ctx, pp_context,
                                   src_surface,
                                   src_rect,
                                   dst_surface,
                                   dst_rect,
                                   pp_index,
                                   filter_param);

    if (va_status == VA_STATUS_SUCCESS) {
        gen8_pp_states_setup(ctx, pp_context);
        gen9_pp_pipeline_setup(ctx, pp_context);
    }

    return va_status;
}

static void
gen9_vpp_scaling_sample_state(VADriverContextP ctx,
                              struct i965_gpe_context *gpe_context,
                              VARectangle *src_rect,
                              VARectangle *dst_rect)
{
    struct gen8_sampler_state *sampler_state;

    if (gpe_context == NULL || !src_rect || !dst_rect)
        return;
    dri_bo_map(gpe_context->sampler.bo, 1);

    if (gpe_context->sampler.bo->virtual == NULL)
        return;

    assert(gpe_context->sampler.bo->virtual);

    sampler_state = (struct gen8_sampler_state *)
                    (gpe_context->sampler.bo->virtual + gpe_context->sampler.offset);

    memset(sampler_state, 0, sizeof(*sampler_state));

    if ((src_rect->width == dst_rect->width) &&
        (src_rect->height == dst_rect->height)) {
        sampler_state->ss0.min_filter = I965_MAPFILTER_NEAREST;
        sampler_state->ss0.mag_filter = I965_MAPFILTER_NEAREST;
    } else {
        sampler_state->ss0.min_filter = I965_MAPFILTER_LINEAR;
        sampler_state->ss0.mag_filter = I965_MAPFILTER_LINEAR;
    }

    sampler_state->ss3.r_wrap_mode = I965_TEXCOORDMODE_CLAMP;
    sampler_state->ss3.s_wrap_mode = I965_TEXCOORDMODE_CLAMP;
    sampler_state->ss3.t_wrap_mode = I965_TEXCOORDMODE_CLAMP;

    dri_bo_unmap(gpe_context->sampler.bo);
}

void
gen9_post_processing_context_init(VADriverContextP ctx,
                                  void *data,
                                  struct intel_batchbuffer *batch)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct i965_post_processing_context *pp_context = data;
    struct i965_gpe_context *gpe_context;

    gen8_post_processing_context_common_init(ctx, data, pp_modules_gen9, ARRAY_ELEMS(pp_modules_gen9), batch);
    avs_init_state(&pp_context->pp_avs_context.state, &gen9_avs_config);

    pp_context->intel_post_processing = gen9_post_processing;

    gpe_context = &pp_context->scaling_gpe_context;
    gen8_gpe_load_kernels(ctx, gpe_context, pp_common_scaling_gen9, ARRAY_ELEMS(pp_common_scaling_gen9));
    gpe_context->idrt.entry_size = ALIGN(sizeof(struct gen8_interface_descriptor_data), 64);
    gpe_context->idrt.max_entries = ALIGN(ARRAY_ELEMS(pp_common_scaling_gen9), 2);
    gpe_context->sampler.entry_size = ALIGN(sizeof(struct gen8_sampler_state), 64);
    gpe_context->sampler.max_entries = 1;
    gpe_context->curbe.length = ALIGN(sizeof(struct scaling_input_parameter), 64);

    gpe_context->surface_state_binding_table.max_entries = MAX_SCALING_SURFACES;
    gpe_context->surface_state_binding_table.binding_table_offset = 0;
    gpe_context->surface_state_binding_table.surface_state_offset = ALIGN(MAX_SCALING_SURFACES * 4, 64);
    gpe_context->surface_state_binding_table.length = ALIGN(MAX_SCALING_SURFACES * 4, 64) + ALIGN(MAX_SCALING_SURFACES * SURFACE_STATE_PADDED_SIZE_GEN9, 64);

    if (i965->intel.eu_total > 0) {
        gpe_context->vfe_state.max_num_threads = i965->intel.eu_total * 6;
    } else {
        if (i965->intel.has_bsd2)
            gpe_context->vfe_state.max_num_threads = 300;
        else
            gpe_context->vfe_state.max_num_threads = 60;
    }

    gpe_context->vfe_state.curbe_allocation_size = 37;
    gpe_context->vfe_state.urb_entry_size = 16;
    gpe_context->vfe_state.num_urb_entries = 127;
    gpe_context->vfe_state.gpgpu_mode = 0;

    gen8_gpe_context_init(ctx, gpe_context);
    pp_context->scaling_gpe_context_initialized |= (VPPGPE_8BIT_8BIT | VPPGPE_10BIT_10BIT | VPPGPE_10BIT_8BIT | VPPGPE_8BIT_420_RGB32);

    gpe_context = &pp_context->clear_gpe_context;
    gen8_gpe_load_kernels(ctx, gpe_context, pp_clear_gen9, ARRAY_ELEMS(pp_clear_gen9));
    gpe_context->idrt.entry_size = ALIGN(sizeof(struct gen8_interface_descriptor_data), 64);
    gpe_context->idrt.max_entries = ALIGN(ARRAY_ELEMS(pp_clear_gen9), 2);
    gpe_context->sampler.entry_size = ALIGN(sizeof(struct gen8_sampler_state), 64);
    gpe_context->sampler.max_entries = 1;
    gpe_context->curbe.length = ALIGN(sizeof(struct clear_input_parameter), 64);

    gpe_context->surface_state_binding_table.max_entries = MAX_SCALING_SURFACES;
    gpe_context->surface_state_binding_table.binding_table_offset = 0;
    gpe_context->surface_state_binding_table.surface_state_offset = ALIGN(MAX_SCALING_SURFACES * 4, 64);
    gpe_context->surface_state_binding_table.length = ALIGN(MAX_SCALING_SURFACES * 4, 64) + ALIGN(MAX_SCALING_SURFACES * SURFACE_STATE_PADDED_SIZE_GEN9, 64);

    if (i965->intel.eu_total > 0) {
        gpe_context->vfe_state.max_num_threads = i965->intel.eu_total * 6;
    } else {
        if (i965->intel.has_bsd2)
            gpe_context->vfe_state.max_num_threads = 300;
        else
            gpe_context->vfe_state.max_num_threads = 60;
    }

    gpe_context->vfe_state.curbe_allocation_size = 37;
    gpe_context->vfe_state.urb_entry_size = 16;
    gpe_context->vfe_state.num_urb_entries = 127;
    gpe_context->vfe_state.gpgpu_mode = 0;

    gen8_gpe_context_init(ctx, gpe_context);
    pp_context->clear_gpe_context_initialized = 1;

    return;
}

static void
gen9_add_dri_buffer_2d_gpe_surface(VADriverContextP ctx,
                                   struct i965_gpe_context *gpe_context,
                                   dri_bo *bo,
                                   unsigned int bo_offset,
                                   unsigned int width,
                                   unsigned int height,
                                   unsigned int pitch,
                                   int is_media_block_rw,
                                   unsigned int format,
                                   int index,
                                   int is_16bit)
{
    struct i965_gpe_resource gpe_resource;
    struct i965_gpe_surface gpe_surface;

    i965_dri_object_to_2d_gpe_resource(&gpe_resource, bo, width, height, pitch);
    memset(&gpe_surface, 0, sizeof(gpe_surface));
    gpe_surface.gpe_resource = &gpe_resource;
    gpe_surface.is_2d_surface = 1;
    gpe_surface.is_media_block_rw = !!is_media_block_rw;
    gpe_surface.cacheability_control = DEFAULT_MOCS;
    gpe_surface.format = format;
    gpe_surface.is_override_offset = 1;
    gpe_surface.offset = bo_offset;
    gpe_surface.is_16bpp = is_16bit;

    gen9_gpe_context_add_surface(gpe_context, &gpe_surface, index);

    i965_free_gpe_resource(&gpe_resource);
}

static void
gen9_run_kernel_media_object_walker(VADriverContextP ctx,
                                    struct intel_batchbuffer *batch,
                                    struct i965_gpe_context *gpe_context,
                                    struct gpe_media_object_walker_parameter *param)
{
    if (!batch || !gpe_context || !param)
        return;

    intel_batchbuffer_start_atomic(batch, 0x1000);

    intel_batchbuffer_emit_mi_flush(batch);

    gen9_gpe_pipeline_setup(ctx, gpe_context, batch);
    gen8_gpe_media_object_walker(ctx, gpe_context, batch, param);
    gen8_gpe_media_state_flush(ctx, gpe_context, batch);

    gen9_gpe_pipeline_end(ctx, gpe_context, batch);

    intel_batchbuffer_end_atomic(batch);

    intel_batchbuffer_flush(batch);
    return;
}

static void
gen9_gpe_context_p010_scaling_curbe(VADriverContextP ctx,
                                    struct i965_gpe_context *gpe_context,
                                    VARectangle *src_rect,
                                    struct i965_surface *src_surface,
                                    VARectangle *dst_rect,
                                    struct i965_surface *dst_surface)
{
    struct scaling_input_parameter *scaling_curbe;
    float src_width, src_height;
    float coeff;
    unsigned int fourcc;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    scaling_curbe = i965_gpe_context_map_curbe(gpe_context);

    if (!scaling_curbe)
        return;

    memset(scaling_curbe, 0, sizeof(struct scaling_input_parameter));

    scaling_curbe->bti_input = BTI_SCALING_INPUT_Y;
    scaling_curbe->bti_output = BTI_SCALING_OUTPUT_Y;

    /* As the src_rect/dst_rect is already checked, it is skipped.*/
    scaling_curbe->x_dst     = dst_rect->x;
    scaling_curbe->y_dst     = dst_rect->y;

    src_width = src_rect->x + src_rect->width;
    src_height = src_rect->y + src_rect->height;

    scaling_curbe->inv_width = 1 / src_width;
    scaling_curbe->inv_height = 1 / src_height;

    coeff = (float)(src_rect->width) / dst_rect->width;
    scaling_curbe->x_factor = coeff / src_width;
    scaling_curbe->x_orig = (float)(src_rect->x) / src_width;

    coeff = (float)(src_rect->height) / dst_rect->height;
    scaling_curbe->y_factor = coeff / src_height;
    scaling_curbe->y_orig = (float)(src_rect->y) / src_height;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);
    if (fourcc == VA_FOURCC_P010) {
        scaling_curbe->dw2.src_packed = 1;
        scaling_curbe->dw2.src_msb = 1;
    }
    /* I010 will use LSB */

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (fourcc == VA_FOURCC_P010) {
        scaling_curbe->dw2.dst_packed = 1;
        scaling_curbe->dw2.dst_msb = 1;
    }
    /* I010 will use LSB */

    i965_gpe_context_unmap_curbe(gpe_context);
}

static bool
gen9_pp_context_get_surface_conf(VADriverContextP ctx,
                                 struct i965_surface *surface,
                                 VARectangle *rect,
                                 int *width,
                                 int *height,
                                 int *pitch,
                                 int *bo_offset)
{
    unsigned int fourcc;
    if (!rect || !surface || !width || !height || !pitch || !bo_offset)
        return false;

    if (surface->base == NULL)
        return false;

    fourcc = pp_get_surface_fourcc(ctx, surface);
    if (surface->type == I965_SURFACE_TYPE_SURFACE) {
        struct object_surface *obj_surface;

        obj_surface = (struct object_surface *)surface->base;
        width[0] = MIN(rect->x + rect->width, obj_surface->orig_width);
        height[0] = MIN(rect->y + rect->height, obj_surface->orig_height);
        pitch[0] = obj_surface->width;
        bo_offset[0] = 0;

        if (fourcc == VA_FOURCC_RGBX ||
            fourcc == VA_FOURCC_RGBA ||
            fourcc == VA_FOURCC_BGRX ||
            fourcc == VA_FOURCC_BGRA) {
            /* nothing to do here */
        } else if (fourcc == VA_FOURCC_P010 || fourcc == VA_FOURCC_NV12) {
            width[1] = width[0] / 2;
            height[1] = height[0] / 2;
            pitch[1] = obj_surface->cb_cr_pitch;
            bo_offset[1] = obj_surface->width * obj_surface->y_cb_offset;
        } else if (fourcc == VA_FOURCC_YUY2 || fourcc == VA_FOURCC_UYVY) {
            /* nothing to do here */
        } else {
            width[1] = width[0] / 2;
            height[1] = height[0] / 2;
            pitch[1] = obj_surface->cb_cr_pitch;
            bo_offset[1] = obj_surface->width * obj_surface->y_cb_offset;
            width[2] = width[0] / 2;
            height[2] = height[0] / 2;
            pitch[2] = obj_surface->cb_cr_pitch;
            bo_offset[2] = obj_surface->width * obj_surface->y_cr_offset;
        }

    } else {
        struct object_image *obj_image;

        obj_image = (struct object_image *)surface->base;

        width[0] = MIN(rect->x + rect->width, obj_image->image.width);
        height[0] = MIN(rect->y + rect->height, obj_image->image.height);
        pitch[0] = obj_image->image.pitches[0];
        bo_offset[0] = obj_image->image.offsets[0];

        if (fourcc == VA_FOURCC_RGBX ||
            fourcc == VA_FOURCC_RGBA ||
            fourcc == VA_FOURCC_BGRX ||
            fourcc == VA_FOURCC_BGRA) {
            /* nothing to do here */
        } else if (fourcc == VA_FOURCC_P010 || fourcc == VA_FOURCC_NV12) {
            width[1] = width[0] / 2;
            height[1] = height[0] / 2;
            pitch[1] = obj_image->image.pitches[1];
            bo_offset[1] = obj_image->image.offsets[1];
        } else if (fourcc == VA_FOURCC_YUY2 || fourcc == VA_FOURCC_UYVY) {
            /* nothing to do here */
        } else {
            int u = 1, v = 2;

            if (fourcc == VA_FOURCC_YV12 || fourcc == VA_FOURCC_IMC1)
                u = 2, v = 1;

            width[1] = width[0] / 2;
            height[1] = height[0] / 2;
            pitch[1] = obj_image->image.pitches[u];
            bo_offset[1] = obj_image->image.offsets[u];
            width[2] = width[0] / 2;
            height[2] = height[0] / 2;
            pitch[2] = obj_image->image.pitches[v];
            bo_offset[2] = obj_image->image.offsets[v];
        }

    }

    return true;
}

static void
gen9_gpe_context_p010_scaling_surfaces(VADriverContextP ctx,
                                       struct i965_gpe_context *gpe_context,
                                       VARectangle *src_rect,
                                       struct i965_surface *src_surface,
                                       VARectangle *dst_rect,
                                       struct i965_surface *dst_surface)
{
    unsigned int fourcc;
    int width[3], height[3], pitch[3], bo_offset[3];
    dri_bo *bo;
    struct object_surface *obj_surface;
    struct object_image *obj_image;
    int bti;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    if (src_surface->base == NULL || dst_surface->base == NULL)
        return;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    if (src_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)src_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)src_surface->base;
        bo = obj_image->bo;
    }

    bti = 0;
    if (gen9_pp_context_get_surface_conf(ctx, src_surface, src_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_INPUT_Y;
        /* Input surface */
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 0,
                                           I965_SURFACEFORMAT_R16_UNORM,
                                           bti, 1);
        if (fourcc == VA_FOURCC_P010) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R16G16_UNORM,
                                               bti + 1, 1);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R16_UNORM,
                                               bti + 1, 1);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 0,
                                               I965_SURFACEFORMAT_R16_UNORM,
                                               bti + 2, 1);
        }
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (dst_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)dst_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)dst_surface->base;
        bo = obj_image->bo;
    }

    if (gen9_pp_context_get_surface_conf(ctx, dst_surface, dst_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_OUTPUT_Y;
        /* Input surface */
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R16_UINT,
                                           bti, 1);
        if (fourcc == VA_FOURCC_P010) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1] * 2, height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R16_UINT,
                                               bti + 1, 1);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R16_UINT,
                                               bti + 1, 1);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 1,
                                               I965_SURFACEFORMAT_R16_UINT,
                                               bti + 2, 1);
        }
    }

    return;
}

VAStatus
gen9_p010_scaling_post_processing(
    VADriverContextP   ctx,
    struct i965_post_processing_context *pp_context,
    struct i965_surface *src_surface,
    VARectangle *src_rect,
    struct i965_surface *dst_surface,
    VARectangle *dst_rect)
{
    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct intel_vpp_kernel_walker_parameter kernel_walker_param;

    if (!pp_context || !src_surface || !src_rect || !dst_surface || !dst_rect)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!(pp_context->scaling_gpe_context_initialized & VPPGPE_10BIT_10BIT))
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    gpe_context = &pp_context->scaling_gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_vpp_scaling_sample_state(ctx, gpe_context, src_rect, dst_rect);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    gen9_gpe_context_p010_scaling_curbe(ctx, gpe_context,
                                        src_rect, src_surface,
                                        dst_rect, dst_surface);

    gen9_gpe_context_p010_scaling_surfaces(ctx, gpe_context,
                                           src_rect, src_surface,
                                           dst_rect, dst_surface);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = ALIGN(dst_rect->width, 16) >> 4;
    kernel_walker_param.resolution_y = ALIGN(dst_rect->height, 16) >> 4;
    kernel_walker_param.no_dependency = 1;

    intel_vpp_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);
    media_object_walker_param.interface_offset = 0;
    gen9_run_kernel_media_object_walker(ctx, pp_context->batch,
                                        gpe_context,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_gpe_context_yuv420p8_scaling_curbe(VADriverContextP ctx,
                                        struct i965_gpe_context *gpe_context,
                                        VARectangle *src_rect,
                                        struct i965_surface *src_surface,
                                        VARectangle *dst_rect,
                                        struct i965_surface *dst_surface)
{
    struct scaling_input_parameter *scaling_curbe;
    float src_width, src_height;
    float coeff;
    unsigned int fourcc;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    scaling_curbe = i965_gpe_context_map_curbe(gpe_context);

    if (!scaling_curbe)
        return;

    memset(scaling_curbe, 0, sizeof(struct scaling_input_parameter));

    scaling_curbe->bti_input = BTI_SCALING_INPUT_Y;
    scaling_curbe->bti_output = BTI_SCALING_OUTPUT_Y;

    /* As the src_rect/dst_rect is already checked, it is skipped.*/
    scaling_curbe->x_dst     = dst_rect->x;
    scaling_curbe->y_dst     = dst_rect->y;

    src_width = src_rect->x + src_rect->width;
    src_height = src_rect->y + src_rect->height;

    scaling_curbe->inv_width = 1 / src_width;
    scaling_curbe->inv_height = 1 / src_height;

    coeff = (float)(src_rect->width) / dst_rect->width;
    scaling_curbe->x_factor = coeff / src_width;
    scaling_curbe->x_orig = (float)(src_rect->x) / src_width;

    coeff = (float)(src_rect->height) / dst_rect->height;
    scaling_curbe->y_factor = coeff / src_height;
    scaling_curbe->y_orig = (float)(src_rect->y) / src_height;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);
    if (fourcc == VA_FOURCC_NV12) {
        scaling_curbe->dw2.src_packed = 1;
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (fourcc == VA_FOURCC_NV12) {
        scaling_curbe->dw2.dst_packed = 1;
    }

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
gen9_gpe_context_yuv420p8_scaling_surfaces(VADriverContextP ctx,
                                           struct i965_gpe_context *gpe_context,
                                           VARectangle *src_rect,
                                           struct i965_surface *src_surface,
                                           VARectangle *dst_rect,
                                           struct i965_surface *dst_surface)
{
    unsigned int fourcc;
    int width[3], height[3], pitch[3], bo_offset[3];
    dri_bo *bo;
    struct object_surface *obj_surface;
    struct object_image *obj_image;
    int bti;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    if (src_surface->base == NULL || dst_surface->base == NULL)
        return;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    if (src_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)src_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)src_surface->base;
        bo = obj_image->bo;
    }

    bti = 0;
    if (gen9_pp_context_get_surface_conf(ctx, src_surface, src_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_INPUT_Y;
        /* Input surface */
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 0,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           bti, 0);
        if (fourcc == VA_FOURCC_NV12) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R8G8_UNORM,
                                               bti + 1, 0);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               bti + 1, 0);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 0,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               bti + 2, 0);
        }
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (dst_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)dst_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)dst_surface->base;
        bo = obj_image->bo;
    }

    if (gen9_pp_context_get_surface_conf(ctx, dst_surface, dst_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_OUTPUT_Y;
        /* Input surface */
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R8_UINT,
                                           bti, 0);
        if (fourcc == VA_FOURCC_NV12) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1] * 2, height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R16_UINT,
                                               bti + 1, 0);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti + 1, 0);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti + 2, 0);
        }
    }

    return;
}

VAStatus
gen9_yuv420p8_scaling_post_processing(
    VADriverContextP   ctx,
    struct i965_post_processing_context *pp_context,
    struct i965_surface *src_surface,
    VARectangle *src_rect,
    struct i965_surface *dst_surface,
    VARectangle *dst_rect)
{
    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct intel_vpp_kernel_walker_parameter kernel_walker_param;

    if (!pp_context || !src_surface || !src_rect || !dst_surface || !dst_rect)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!(pp_context->scaling_gpe_context_initialized & VPPGPE_8BIT_8BIT))
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    gpe_context = &pp_context->scaling_gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_vpp_scaling_sample_state(ctx, gpe_context, src_rect, dst_rect);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    gen9_gpe_context_yuv420p8_scaling_curbe(ctx, gpe_context,
                                            src_rect, src_surface,
                                            dst_rect, dst_surface);

    gen9_gpe_context_yuv420p8_scaling_surfaces(ctx, gpe_context,
                                               src_rect, src_surface,
                                               dst_rect, dst_surface);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = ALIGN(dst_rect->width, 16) >> 4;
    kernel_walker_param.resolution_y = ALIGN(dst_rect->height, 16) >> 4;
    kernel_walker_param.no_dependency = 1;

    intel_vpp_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);
    media_object_walker_param.interface_offset = 1;
    gen9_run_kernel_media_object_walker(ctx, pp_context->batch,
                                        gpe_context,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_gpe_context_10bit_8bit_scaling_curbe(VADriverContextP ctx,
                                          struct i965_gpe_context *gpe_context,
                                          VARectangle *src_rect,
                                          struct i965_surface *src_surface,
                                          VARectangle *dst_rect,
                                          struct i965_surface *dst_surface)
{
    struct scaling_input_parameter *scaling_curbe;
    float src_width, src_height;
    float coeff;
    unsigned int fourcc;
    int src_format = SRC_FORMAT_P010, dst_format = DST_FORMAT_YUY2;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    scaling_curbe = i965_gpe_context_map_curbe(gpe_context);

    if (!scaling_curbe)
        return;

    memset(scaling_curbe, 0, sizeof(struct scaling_input_parameter));

    scaling_curbe->bti_input = BTI_SCALING_INPUT_Y;
    scaling_curbe->bti_output = BTI_SCALING_OUTPUT_Y;

    /* As the src_rect/dst_rect is already checked, it is skipped.*/
    scaling_curbe->x_dst     = dst_rect->x;
    scaling_curbe->y_dst     = dst_rect->y;

    src_width = src_rect->x + src_rect->width;
    src_height = src_rect->y + src_rect->height;

    scaling_curbe->inv_width = 1 / src_width;
    scaling_curbe->inv_height = 1 / src_height;

    coeff = (float)(src_rect->width) / dst_rect->width;
    scaling_curbe->x_factor = coeff / src_width;
    scaling_curbe->x_orig = (float)(src_rect->x) / src_width;

    coeff = (float)(src_rect->height) / dst_rect->height;
    scaling_curbe->y_factor = coeff / src_height;
    scaling_curbe->y_orig = (float)(src_rect->y) / src_height;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    switch (fourcc) {
    case VA_FOURCC_P010:
        src_format = SRC_FORMAT_P010;
        break;

    case VA_FOURCC_I010:
        src_format = SRC_FORMAT_I010;
        break;

    default:
        break;
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    switch (fourcc) {
    case VA_FOURCC_YUY2:
        dst_format = DST_FORMAT_YUY2;
        break;

    case VA_FOURCC_UYVY:
        dst_format = DST_FORMAT_UYVY;
        break;

    case VA_FOURCC_NV12:
        dst_format = DST_FORMAT_NV12;
        break;

    case VA_FOURCC_I420:
    case VA_FOURCC_IMC3: /* pitch / base address is set via surface_state */
        dst_format = DST_FORMAT_I420;
        break;

    case VA_FOURCC_YV12:
    case VA_FOURCC_IMC1: /* pitch / base address is set via surface_state */
        dst_format = DST_FORMAT_YV12;
        break;

    default:
        break;
    }

    scaling_curbe->dw2.src_format = src_format;
    scaling_curbe->dw2.dst_format = dst_format;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
gen9_gpe_context_10bit_8bit_scaling_surfaces(VADriverContextP ctx,
                                             struct i965_gpe_context *gpe_context,
                                             VARectangle *src_rect,
                                             struct i965_surface *src_surface,
                                             VARectangle *dst_rect,
                                             struct i965_surface *dst_surface)
{
    unsigned int fourcc;
    int width[3], height[3], pitch[3], bo_offset[3];
    dri_bo *bo;
    struct object_surface *obj_surface;
    struct object_image *obj_image;
    int bti;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    if (src_surface->base == NULL || dst_surface->base == NULL)
        return;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    if (src_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)src_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)src_surface->base;
        bo = obj_image->bo;
    }

    bti = 0;
    if (gen9_pp_context_get_surface_conf(ctx, src_surface, src_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_INPUT_Y;
        /* Input surface */
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 0,
                                           I965_SURFACEFORMAT_R16_UNORM,
                                           bti, 1);
        if (fourcc == VA_FOURCC_P010) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R16G16_UNORM,
                                               bti + 1, 1);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R16_UNORM,
                                               bti + 1, 1);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 0,
                                               I965_SURFACEFORMAT_R16_UNORM,
                                               bti + 2, 1);
        }
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (dst_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)dst_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)dst_surface->base;
        bo = obj_image->bo;
    }

    if (gen9_pp_context_get_surface_conf(ctx, dst_surface, dst_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        bti = BTI_SCALING_OUTPUT_Y;
        /* Output surface */

        if (fourcc == VA_FOURCC_YUY2 || fourcc == VA_FOURCC_UYVY) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[0],
                                               width[0] * 2, height[0],
                                               pitch[0], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti, 0);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[0],
                                               width[0], height[0],
                                               pitch[0], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti, 0);

            if (fourcc == VA_FOURCC_NV12) {
                gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                                   bo_offset[1],
                                                   width[1] * 2, height[1],
                                                   pitch[1], 1,
                                                   I965_SURFACEFORMAT_R16_UINT,
                                                   bti + 1, 0);
            } else {
                gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                                   bo_offset[1],
                                                   width[1], height[1],
                                                   pitch[1], 1,
                                                   I965_SURFACEFORMAT_R8_UINT,
                                                   bti + 1, 0);

                gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                                   bo_offset[2],
                                                   width[2], height[2],
                                                   pitch[2], 1,
                                                   I965_SURFACEFORMAT_R8_UINT,
                                                   bti + 2, 0);
            }
        }
    }
}

VAStatus
gen9_10bit_8bit_scaling_post_processing(VADriverContextP   ctx,
                                        struct i965_post_processing_context *pp_context,
                                        struct i965_surface *src_surface,
                                        VARectangle *src_rect,
                                        struct i965_surface *dst_surface,
                                        VARectangle *dst_rect)
{
    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct intel_vpp_kernel_walker_parameter kernel_walker_param;

    if (!pp_context || !src_surface || !src_rect || !dst_surface || !dst_rect)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!(pp_context->scaling_gpe_context_initialized & VPPGPE_10BIT_10BIT))
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    gpe_context = &pp_context->scaling_gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_vpp_scaling_sample_state(ctx, gpe_context, src_rect, dst_rect);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    gen9_gpe_context_10bit_8bit_scaling_curbe(ctx, gpe_context,
                                              src_rect, src_surface,
                                              dst_rect, dst_surface);

    gen9_gpe_context_10bit_8bit_scaling_surfaces(ctx, gpe_context,
                                                 src_rect, src_surface,
                                                 dst_rect, dst_surface);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = ALIGN(dst_rect->width, 16) >> 4;
    kernel_walker_param.resolution_y = ALIGN(dst_rect->height, 16) >> 4;
    kernel_walker_param.no_dependency = 1;

    intel_vpp_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);
    media_object_walker_param.interface_offset = 2;
    gen9_run_kernel_media_object_walker(ctx, pp_context->batch,
                                        gpe_context,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_gpe_context_8bit_420_rgb32_scaling_curbe(VADriverContextP ctx,
                                              struct i965_gpe_context *gpe_context,
                                              VARectangle *src_rect,
                                              struct i965_surface *src_surface,
                                              VARectangle *dst_rect,
                                              struct i965_surface *dst_surface)
{
    struct scaling_input_parameter *scaling_curbe;
    float src_width, src_height;
    float coeff;
    unsigned int fourcc;
    int src_format = SRC_FORMAT_I420, dst_format = DST_FORMAT_RGBX;
    const float * yuv_to_rgb_coefs;
    size_t yuv_to_rgb_coefs_size;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    scaling_curbe = i965_gpe_context_map_curbe(gpe_context);

    if (!scaling_curbe)
        return;

    memset(scaling_curbe, 0, sizeof(struct scaling_input_parameter));

    scaling_curbe->bti_input = BTI_SCALING_INPUT_Y;
    scaling_curbe->bti_output = BTI_SCALING_OUTPUT_Y;

    /* As the src_rect/dst_rect is already checked, it is skipped.*/
    scaling_curbe->x_dst     = dst_rect->x;
    scaling_curbe->y_dst     = dst_rect->y;

    src_width = src_rect->x + src_rect->width;
    src_height = src_rect->y + src_rect->height;

    scaling_curbe->inv_width = 1 / src_width;
    scaling_curbe->inv_height = 1 / src_height;

    coeff = (float)(src_rect->width) / dst_rect->width;
    scaling_curbe->x_factor = coeff / src_width;
    scaling_curbe->x_orig = (float)(src_rect->x) / src_width;

    coeff = (float)(src_rect->height) / dst_rect->height;
    scaling_curbe->y_factor = coeff / src_height;
    scaling_curbe->y_orig = (float)(src_rect->y) / src_height;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    switch (fourcc) {
    case VA_FOURCC_I420:
    case VA_FOURCC_IMC3: /* pitch / base address is set via surface_state */
        src_format = SRC_FORMAT_I420;
        break;

    case VA_FOURCC_NV12:
        src_format = SRC_FORMAT_NV12;
        break;

    case VA_FOURCC_YV12:
    case VA_FOURCC_IMC1: /* pitch / base address is set via surface_state */
        src_format = SRC_FORMAT_YV12;
        break;

    default:
        break;
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    switch (fourcc) {
    case VA_FOURCC_RGBX:
        dst_format = DST_FORMAT_RGBX;
        break;

    case VA_FOURCC_RGBA:
        dst_format = DST_FORMAT_RGBA;
        break;

    case VA_FOURCC_BGRX:
        dst_format = DST_FORMAT_BGRX;
        break;

    case VA_FOURCC_BGRA:
        dst_format = DST_FORMAT_BGRA;
        break;

    default:
        break;
    }

    scaling_curbe->dw2.src_format = src_format;
    scaling_curbe->dw2.dst_format = dst_format;

    yuv_to_rgb_coefs = i915_color_standard_to_coefs(i915_filter_to_color_standard(src_surface->flags & VA_SRC_COLOR_MASK), &yuv_to_rgb_coefs_size);
    memcpy(&scaling_curbe->coef_ry, yuv_to_rgb_coefs, yuv_to_rgb_coefs_size);

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
gen9_gpe_context_8bit_420_rgb32_scaling_surfaces(VADriverContextP ctx,
                                                 struct i965_gpe_context *gpe_context,
                                                 VARectangle *src_rect,
                                                 struct i965_surface *src_surface,
                                                 VARectangle *dst_rect,
                                                 struct i965_surface *dst_surface)
{
    unsigned int fourcc;
    int width[3], height[3], pitch[3], bo_offset[3];
    dri_bo *bo;
    struct object_surface *obj_surface;
    struct object_image *obj_image;
    int bti;

    if ((gpe_context == NULL) ||
        (src_rect == NULL) || (src_surface == NULL) ||
        (dst_rect == NULL) || (dst_surface == NULL))
        return;

    if (src_surface->base == NULL || dst_surface->base == NULL)
        return;

    fourcc = pp_get_surface_fourcc(ctx, src_surface);

    if (src_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)src_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)src_surface->base;
        bo = obj_image->bo;
    }

    if (gen9_pp_context_get_surface_conf(ctx, src_surface, src_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        /* Input surface */
        bti = BTI_SCALING_INPUT_Y;
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 0,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           bti, 0);

        if (fourcc == VA_FOURCC_NV12) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R8G8_UNORM,
                                               bti + 1, 0);
        } else {
            /* The corresponding shader handles U, V plane in order */
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 0,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               bti + 1, 0);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 0,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               bti + 2, 0);
        }
    }

    fourcc = pp_get_surface_fourcc(ctx, dst_surface);

    if (dst_surface->type == I965_SURFACE_TYPE_SURFACE) {
        obj_surface = (struct object_surface *)dst_surface->base;
        bo = obj_surface->bo;
    } else {
        obj_image = (struct object_image *)dst_surface->base;
        bo = obj_image->bo;
    }

    if (gen9_pp_context_get_surface_conf(ctx, dst_surface, dst_rect,
                                         width, height, pitch,
                                         bo_offset)) {
        assert(fourcc == VA_FOURCC_RGBX ||
               fourcc == VA_FOURCC_RGBA ||
               fourcc == VA_FOURCC_BGRX ||
               fourcc == VA_FOURCC_BGRA);
        assert(width[0] * 4 <= pitch[0]);

        /* output surface */
        bti = BTI_SCALING_OUTPUT_Y;
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0] * 4, height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R8_UINT,
                                           bti, 0);
    }
}

VAStatus
gen9_8bit_420_rgb32_scaling_post_processing(VADriverContextP   ctx,
                                            struct i965_post_processing_context *pp_context,
                                            struct i965_surface *src_surface,
                                            VARectangle *src_rect,
                                            struct i965_surface *dst_surface,
                                            VARectangle *dst_rect)
{
    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct intel_vpp_kernel_walker_parameter kernel_walker_param;

    if (!pp_context || !src_surface || !src_rect || !dst_surface || !dst_rect)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!(pp_context->scaling_gpe_context_initialized & VPPGPE_8BIT_420_RGB32))
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    gpe_context = &pp_context->scaling_gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_vpp_scaling_sample_state(ctx, gpe_context, src_rect, dst_rect);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    gen9_gpe_context_8bit_420_rgb32_scaling_curbe(ctx, gpe_context,
                                                  src_rect, src_surface,
                                                  dst_rect, dst_surface);

    gen9_gpe_context_8bit_420_rgb32_scaling_surfaces(ctx, gpe_context,
                                                     src_rect, src_surface,
                                                     dst_rect, dst_surface);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = ALIGN(dst_rect->width, 16) >> 4;
    kernel_walker_param.resolution_y = ALIGN(dst_rect->height, 16) >> 4;
    kernel_walker_param.no_dependency = 1;

    intel_vpp_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);
    media_object_walker_param.interface_offset = 3;
    gen9_run_kernel_media_object_walker(ctx, pp_context->batch,
                                        gpe_context,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_clear_surface_sample_state(VADriverContextP ctx,
                                struct i965_gpe_context *gpe_context,
                                const struct object_surface *obj_surface)
{
    struct gen8_sampler_state *sampler_state;

    if (gpe_context == NULL)
        return;

    dri_bo_map(gpe_context->sampler.bo, 1);

    if (gpe_context->sampler.bo->virtual == NULL)
        return;

    sampler_state = (struct gen8_sampler_state *)(gpe_context->sampler.bo->virtual + gpe_context->sampler.offset);

    memset(sampler_state, 0, sizeof(*sampler_state));

    dri_bo_unmap(gpe_context->sampler.bo);
}

static void
gen9_clear_surface_curbe(VADriverContextP ctx,
                         struct i965_gpe_context *gpe_context,
                         const struct object_surface *obj_surface,
                         unsigned int color)
{
    struct clear_input_parameter  *clear_curbe;

    if (gpe_context == NULL || !obj_surface)
        return;

    clear_curbe = i965_gpe_context_map_curbe(gpe_context);

    if (!clear_curbe)
        return;

    memset(clear_curbe, 0, sizeof(struct clear_input_parameter));
    clear_curbe->color = color;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
gen9_clear_surface_state(VADriverContextP ctx,
                         struct i965_gpe_context *gpe_context,
                         const struct object_surface *obj_surface)
{
    struct i965_surface src_surface;
    VARectangle rect;
    dri_bo *bo;
    unsigned int fourcc;
    int width[3], height[3], pitch[3], bo_offset[3];
    int bti;

    src_surface.base  = (struct object_base *)obj_surface;
    src_surface.type  = I965_SURFACE_TYPE_SURFACE;
    src_surface.flags = I965_SURFACE_FLAG_FRAME;

    fourcc = obj_surface->fourcc;
    rect.x = 0;
    rect.y = 0;
    rect.width = obj_surface->orig_width;
    rect.height = obj_surface->orig_height;

    gen9_pp_context_get_surface_conf(ctx, &src_surface,
                                     &rect,
                                     width,
                                     height,
                                     pitch,
                                     bo_offset);

    bti = 1;
    bo = obj_surface->bo;

    if (fourcc == VA_FOURCC_RGBA ||
        fourcc == VA_FOURCC_RGBX ||
        fourcc == VA_FOURCC_BGRA ||
        fourcc == VA_FOURCC_BGRX) {
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0] * 4, height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R8_UINT,
                                           bti, 0);
    } else if (fourcc == VA_FOURCC_YUY2 || fourcc == VA_FOURCC_UYVY) {
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0] * 2, height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R8_UINT,
                                           bti, 0);
    } else {
        gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                           bo_offset[0],
                                           width[0], height[0],
                                           pitch[0], 1,
                                           I965_SURFACEFORMAT_R8_UINT,
                                           bti, 0);

        if (fourcc == VA_FOURCC_NV12) {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1] * 2, height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti + 1, 0);
        } else {
            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[1],
                                               width[1], height[1],
                                               pitch[1], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti + 1, 0);

            gen9_add_dri_buffer_2d_gpe_surface(ctx, gpe_context, bo,
                                               bo_offset[2],
                                               width[2], height[2],
                                               pitch[2], 1,
                                               I965_SURFACEFORMAT_R8_UINT,
                                               bti + 2, 0);
        }
    }
}

void
gen9_clear_surface(VADriverContextP ctx,
                   struct i965_post_processing_context *pp_context,
                   const struct object_surface *obj_surface,
                   unsigned int color)
{
    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct intel_vpp_kernel_walker_parameter kernel_walker_param;
    int index = 0;

    if (!pp_context || !obj_surface)
        return;

    if (!pp_context->clear_gpe_context_initialized)
        return;

    switch (obj_surface->fourcc) {
    case VA_FOURCC_NV12:
        index = 0;
        break;

    case VA_FOURCC_I420:
    case VA_FOURCC_YV12:
    case VA_FOURCC_IMC1:
    case VA_FOURCC_IMC3:
        index = 1;
        break;

    case VA_FOURCC_YUY2:
        index = 2;
        break;

    case VA_FOURCC_UYVY:
        index = 3;
        break;

    case VA_FOURCC_RGBA:
    case VA_FOURCC_RGBX:
        index = 4;
        break;

    case VA_FOURCC_BGRA:
    case VA_FOURCC_BGRX:
        index = 5;
        break;

    default:
        /* TODO: add support for other fourccs */
        return;
    }

    gpe_context = &pp_context->clear_gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_clear_surface_sample_state(ctx, gpe_context, obj_surface);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    gen9_clear_surface_curbe(ctx, gpe_context, obj_surface, color);
    gen9_clear_surface_state(ctx, gpe_context, obj_surface);
    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = ALIGN(obj_surface->orig_width, 16) >> 4;
    kernel_walker_param.resolution_y = ALIGN(obj_surface->orig_height, 16) >> 4;
    kernel_walker_param.no_dependency = 1;

    intel_vpp_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);
    media_object_walker_param.interface_offset = index;
    gen9_run_kernel_media_object_walker(ctx,
                                        pp_context->batch,
                                        gpe_context,
                                        &media_object_walker_param);
}
