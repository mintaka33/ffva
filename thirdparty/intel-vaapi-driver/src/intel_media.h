/*
 * Copyright (C) 2012 Intel Corporation
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
 */

#ifndef INTEL_MEDIA_H
#define INTEL_MEDIA_H

#include <stdint.h>
#include <stdlib.h>

#include <va/va.h>
#include <intel_bufmgr.h>

typedef struct gen_codec_surface GenCodecSurface;

struct gen_codec_surface {
    int frame_store_id;
};

typedef struct gen_avc_surface GenAvcSurface;
struct gen_avc_surface {
    GenCodecSurface base;
    dri_bo *dmv_top;
    dri_bo *dmv_bottom;
    int dmv_bottom_flag;
};

extern void gen_free_avc_surface(void **data);


extern int intel_format_convert(float src, int out_int_bits, int out_frac_bits, int out_sign_flag);

typedef struct gen_hevc_surface GenHevcSurface;
struct gen_hevc_surface {
    GenCodecSurface base;
    dri_bo *motion_vector_temporal_bo;
    //Encoding HEVC10:internal surface keep for P010->NV12 , this is only for hevc10 to save the P010->NV12
    struct object_surface *nv12_surface_obj;
    VASurfaceID nv12_surface_id;
    VADriverContextP ctx;
    int has_p010_to_nv12_done;
};

typedef struct gen_vp9_surface GenVP9Surface;
struct gen_vp9_surface {
    GenCodecSurface base;
    uint16_t frame_width;
    uint16_t frame_height;
    dri_bo *motion_vector_temporal_bo;
};

typedef struct vdenc_avc_surface VDEncAvcSurface;
struct vdenc_avc_surface {
    VADriverContextP ctx;
    VASurfaceID scaled_4x_surface_id;
    struct object_surface *scaled_4x_surface_obj;
};

typedef struct vdenc_vp9_surface VDEncVP9Surface;
struct vdenc_vp9_surface {
    VADriverContextP ctx;
    VASurfaceID scaled_4x_surface_id;
    struct object_surface *scaled_4x_surface_obj;
    VASurfaceID scaled_8x_surface_id;
    struct object_surface *scaled_8x_surface_obj;
    VASurfaceID scaled_16x_surface_id;
    struct object_surface *scaled_16x_surface_obj;

    VASurfaceID dys_surface_id;
    struct object_surface *dys_surface_obj;
    VASurfaceID dys_4x_surface_id;
    struct object_surface *dys_4x_surface_obj;
    VASurfaceID dys_8x_surface_id;
    struct object_surface *dys_8x_surface_obj;
    VASurfaceID dys_16x_surface_id;
    struct object_surface *dys_16x_surface_obj;
    int dys_frame_width;
    int dys_frame_height;
    int frame_width;
    int frame_height;
    unsigned int qp_value;
    uint8_t dys_hme_flag;
};

extern void gen_free_hevc_surface(void **data);

extern void gen_free_vp9_surface(void **data);

extern void vdenc_free_avc_surface(void **data);

extern void vdenc_free_vp9_surface(void **data);

#endif /* INTEL_MEDIA_H */
