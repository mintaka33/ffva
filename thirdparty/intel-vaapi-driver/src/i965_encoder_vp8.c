/*
 * Copyright © 2017 Intel Corporation
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
 * Authors:
 *    Xiang, Haihao <haihao.xiang@intel.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_driver.h"

#include "i965_defines.h"
#include "i965_drv_video.h"
#include "i965_encoder.h"
#include "i965_gpe_utils.h"
#include "i965_encoder_vp8.h"
#include "vp8_probs.h"
#include "vpx_quant.h"

#define SCALE_FACTOR_4X                 4
#define SCALE_FACTOR_16X                16

#define MAX_VP8_ENCODER_SURFACES        128

#define MAX_URB_SIZE                    2048 /* In register */
#define NUM_KERNELS_PER_GPE_CONTEXT     1

#define VP8_BRC_KBPS                    1000

#define BRC_KERNEL_CBR                  0x0010
#define BRC_KERNEL_VBR                  0x0020

struct i965_kernel vp8_kernels_brc_init_reset[NUM_VP8_BRC_RESET] = {
    {
        "VP8 BRC Init",
        VP8_BRC_INIT,
        NULL,
        0,
        NULL
    },

    {
        "VP8 BRC Reset",
        VP8_BRC_RESET,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_scaling[NUM_VP8_SCALING] = {
    {
        "VP8 SCALE 4X",
        VP8_SCALING_4X,
        NULL,
        0,
        NULL
    },

    {
        "VP8 SCALE 16",
        VP8_SCALING_16X,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_me[NUM_VP8_ME] = {
    {
        "VP8 ME 4X",
        VP8_ME_4X,
        NULL,
        0,
        NULL
    },

    {
        "VP8 ME 16",
        VP8_ME_16X,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_mbenc[NUM_VP8_MBENC] = {
    {
        "VP8 MBEnc I Frame Dist",
        VP8_MBENC_I_FRAME_DIST,
        NULL,
        0,
        NULL
    },

    {
        "VP8 MBEnc I Frame Luma",
        VP8_MBENC_I_FRAME_LUMA,
        NULL,
        0,
        NULL
    },

    {
        "VP8 MBEnc I Frame Chroma",
        VP8_MBENC_I_FRAME_CHROMA,
        NULL,
        0,
        NULL
    },

    {
        "VP8 MBEnc P Frame",
        VP8_MBENC_P_FRAME,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_mpu[NUM_VP8_MPU] = {
    {
        "VP8 MPU",
        VP8_MPU,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_tpu[NUM_VP8_TPU] = {
    {
        "VP8 TPU",
        VP8_TPU,
        NULL,
        0,
        NULL
    },
};

struct i965_kernel vp8_kernels_brc_update[NUM_VP8_BRC_UPDATE] = {
    {
        "VP8 BRC Update",
        VP8_BRC_UPDATE,
        NULL,
        0,
        NULL
    },
};

static const unsigned char
vp8_num_refs[8] = {
    0, 1, 1, 2, 1, 2, 2, 3
};

static const unsigned int
vp8_search_path[8][16] = {
    // MEMethod: 0
    {
        0x120FF10F, 0x1E22E20D, 0x20E2FF10, 0x2EDD06FC, 0x11D33FF1, 0xEB1FF33D, 0x4EF1F1F1, 0xF1F21211,
        0x0DFFFFE0, 0x11201F1F, 0x1105F1CF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    },
    // MEMethod: 1
    {
        0x120FF10F, 0x1E22E20D, 0x20E2FF10, 0x2EDD06FC, 0x11D33FF1, 0xEB1FF33D, 0x4EF1F1F1, 0xF1F21211,
        0x0DFFFFE0, 0x11201F1F, 0x1105F1CF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    },
    // MEMethod: 2
    {
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    },
    // MEMethod: 3
    {
        0x01010101, 0x11010101, 0x01010101, 0x11010101, 0x01010101, 0x11010101, 0x01010101, 0x11010101,
        0x01010101, 0x11010101, 0x01010101, 0x00010101, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    },
    // MEMethod: 4
    {
        0x0101F00F, 0x0F0F1010, 0xF0F0F00F, 0x01010101, 0x10101010, 0x0F0F0F0F, 0xF0F0F00F, 0x0101F0F0,
        0x01010101, 0x10101010, 0x0F0F1010, 0x0F0F0F0F, 0xF0F0F00F, 0xF0F0F0F0, 0x00000000, 0x00000000
    },
    // MEMethod: 5
    {
        0x0101F00F, 0x0F0F1010, 0xF0F0F00F, 0x01010101, 0x10101010, 0x0F0F0F0F, 0xF0F0F00F, 0x0101F0F0,
        0x01010101, 0x10101010, 0x0F0F1010, 0x0F0F0F0F, 0xF0F0F00F, 0xF0F0F0F0, 0x00000000, 0x00000000
    },
    // MEMethod: 6
    {
        0x120FF10F, 0x1E22E20D, 0x20E2FF10, 0x2EDD06FC, 0x11D33FF1, 0xEB1FF33D, 0x4EF1F1F1, 0xF1F21211,
        0x0DFFFFE0, 0x11201F1F, 0x1105F1CF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    },
    // MEMethod: 7
    {
        0x1F11F10F, 0x2E22E2FE, 0x20E220DF, 0x2EDD06FC, 0x11D33FF1, 0xEB1FF33D, 0x02F1F1F1, 0x1F201111,
        0xF1EFFF0C, 0xF01104F1, 0x10FF0A50, 0x000FF1C0, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    }
};

static const unsigned char
i_frame_vme_costs_vp8[NUM_QP_VP8][4] = {
    {0x05, 0x1f, 0x02, 0x09},
    {0x05, 0x1f, 0x02, 0x09},
    {0x08, 0x2b, 0x03, 0x0e},
    {0x08, 0x2b, 0x03, 0x0e},
    {0x0a, 0x2f, 0x04, 0x12},
    {0x0a, 0x2f, 0x04, 0x12},
    {0x0d, 0x39, 0x05, 0x17},
    {0x0d, 0x39, 0x05, 0x17},
    {0x0d, 0x39, 0x05, 0x17},
    {0x0f, 0x3b, 0x06, 0x1b},
    {0x0f, 0x3b, 0x06, 0x1b},
    {0x19, 0x3d, 0x07, 0x20},
    {0x19, 0x3d, 0x07, 0x20},
    {0x1a, 0x3f, 0x08, 0x24},
    {0x1a, 0x3f, 0x08, 0x24},
    {0x1a, 0x3f, 0x08, 0x24},
    {0x1b, 0x48, 0x09, 0x29},
    {0x1b, 0x48, 0x09, 0x29},
    {0x1d, 0x49, 0x09, 0x2d},
    {0x1d, 0x49, 0x09, 0x2d},
    {0x1d, 0x49, 0x09, 0x2d},
    {0x1d, 0x49, 0x09, 0x2d},
    {0x1e, 0x4a, 0x0a, 0x32},
    {0x1e, 0x4a, 0x0a, 0x32},
    {0x1e, 0x4a, 0x0a, 0x32},
    {0x1e, 0x4a, 0x0a, 0x32},
    {0x1f, 0x4b, 0x0b, 0x36},
    {0x1f, 0x4b, 0x0b, 0x36},
    {0x1f, 0x4b, 0x0b, 0x36},
    {0x28, 0x4c, 0x0c, 0x3b},
    {0x28, 0x4c, 0x0c, 0x3b},
    {0x29, 0x4d, 0x0d, 0x3f},
    {0x29, 0x4d, 0x0d, 0x3f},
    {0x29, 0x4e, 0x0e, 0x44},
    {0x29, 0x4e, 0x0e, 0x44},
    {0x2a, 0x4f, 0x0f, 0x48},
    {0x2a, 0x4f, 0x0f, 0x48},
    {0x2b, 0x58, 0x10, 0x4d},
    {0x2b, 0x58, 0x10, 0x4d},
    {0x2b, 0x58, 0x11, 0x51},
    {0x2b, 0x58, 0x11, 0x51},
    {0x2b, 0x58, 0x11, 0x51},
    {0x2c, 0x58, 0x12, 0x56},
    {0x2c, 0x58, 0x12, 0x56},
    {0x2c, 0x59, 0x13, 0x5a},
    {0x2c, 0x59, 0x13, 0x5a},
    {0x2d, 0x59, 0x14, 0x5f},
    {0x2d, 0x59, 0x14, 0x5f},
    {0x2e, 0x5a, 0x15, 0x63},
    {0x2e, 0x5a, 0x15, 0x63},
    {0x2e, 0x5a, 0x16, 0x68},
    {0x2e, 0x5a, 0x16, 0x68},
    {0x2e, 0x5a, 0x16, 0x68},
    {0x2f, 0x5b, 0x17, 0x6c},
    {0x2f, 0x5b, 0x17, 0x6c},
    {0x38, 0x5b, 0x18, 0x71},
    {0x38, 0x5b, 0x18, 0x71},
    {0x38, 0x5c, 0x19, 0x76},
    {0x38, 0x5c, 0x19, 0x76},
    {0x38, 0x5c, 0x1a, 0x7a},
    {0x38, 0x5c, 0x1a, 0x7a},
    {0x39, 0x5d, 0x1a, 0x7f},
    {0x39, 0x5d, 0x1a, 0x7f},
    {0x39, 0x5d, 0x1b, 0x83},
    {0x39, 0x5d, 0x1b, 0x83},
    {0x39, 0x5e, 0x1c, 0x88},
    {0x39, 0x5e, 0x1c, 0x88},
    {0x3a, 0x5e, 0x1d, 0x8c},
    {0x3a, 0x5e, 0x1d, 0x8c},
    {0x3a, 0x5f, 0x1e, 0x91},
    {0x3a, 0x5f, 0x1e, 0x91},
    {0x3a, 0x5f, 0x1f, 0x95},
    {0x3a, 0x5f, 0x1f, 0x95},
    {0x3a, 0x68, 0x20, 0x9a},
    {0x3a, 0x68, 0x20, 0x9a},
    {0x3b, 0x68, 0x21, 0x9e},
    {0x3b, 0x68, 0x21, 0x9e},
    {0x3b, 0x68, 0x22, 0xa3},
    {0x3b, 0x68, 0x22, 0xa3},
    {0x3b, 0x68, 0x23, 0xa7},
    {0x3b, 0x68, 0x23, 0xa7},
    {0x3c, 0x68, 0x24, 0xac},
    {0x3c, 0x68, 0x24, 0xac},
    {0x3c, 0x68, 0x24, 0xac},
    {0x3c, 0x69, 0x25, 0xb0},
    {0x3c, 0x69, 0x25, 0xb0},
    {0x3c, 0x69, 0x26, 0xb5},
    {0x3c, 0x69, 0x26, 0xb5},
    {0x3d, 0x69, 0x27, 0xb9},
    {0x3d, 0x69, 0x27, 0xb9},
    {0x3d, 0x69, 0x28, 0xbe},
    {0x3d, 0x69, 0x28, 0xbe},
    {0x3d, 0x6a, 0x29, 0xc2},
    {0x3d, 0x6a, 0x29, 0xc2},
    {0x3e, 0x6a, 0x2a, 0xc7},
    {0x3e, 0x6a, 0x2a, 0xc7},
    {0x3e, 0x6a, 0x2b, 0xcb},
    {0x3e, 0x6a, 0x2b, 0xd0},
    {0x3f, 0x6b, 0x2c, 0xd4},
    {0x3f, 0x6b, 0x2d, 0xd9},
    {0x3f, 0x6b, 0x2e, 0xdd},
    {0x48, 0x6b, 0x2f, 0xe2},
    {0x48, 0x6b, 0x2f, 0xe2},
    {0x48, 0x6c, 0x30, 0xe6},
    {0x48, 0x6c, 0x31, 0xeb},
    {0x48, 0x6c, 0x32, 0xf0},
    {0x48, 0x6c, 0x33, 0xf4},
    {0x48, 0x6c, 0x34, 0xf9},
    {0x49, 0x6d, 0x35, 0xfd},
    {0x49, 0x6d, 0x36, 0xff},
    {0x49, 0x6d, 0x37, 0xff},
    {0x49, 0x6d, 0x38, 0xff},
    {0x49, 0x6e, 0x3a, 0xff},
    {0x49, 0x6e, 0x3b, 0xff},
    {0x4a, 0x6e, 0x3c, 0xff},
    {0x4a, 0x6f, 0x3d, 0xff},
    {0x4a, 0x6f, 0x3d, 0xff},
    {0x4a, 0x6f, 0x3e, 0xff},
    {0x4a, 0x6f, 0x3f, 0xff},
    {0x4a, 0x6f, 0x40, 0xff},
    {0x4b, 0x78, 0x41, 0xff},
    {0x4b, 0x78, 0x42, 0xff},
    {0x4b, 0x78, 0x43, 0xff},
    {0x4b, 0x78, 0x44, 0xff},
    {0x4b, 0x78, 0x46, 0xff},
    {0x4c, 0x78, 0x47, 0xff},
    {0x4c, 0x79, 0x49, 0xff},
    {0x4c, 0x79, 0x4a, 0xff}
};

static const unsigned char
mainref_table_vp8[8] = {
    0, 1, 2, 9, 3, 13, 14, 57
};

static const unsigned int
cost_table_vp8[NUM_QP_VP8][7] = {
    {0x398f0500, 0x6f6f6f6f, 0x0000006f, 0x06040402, 0x1a0c0907, 0x08, 0x0e},
    {0x3b8f0600, 0x6f6f6f6f, 0x0000006f, 0x06040402, 0x1a0c0907, 0x0a, 0x11},
    {0x3e8f0700, 0x6f6f6f6f, 0x0000006f, 0x06040402, 0x1a0c0907, 0x0c, 0x14},
    {0x488f0800, 0x6f6f6f6f, 0x0000006f, 0x06040402, 0x1a0c0907, 0x0f, 0x18},
    {0x498f0a00, 0x6f6f6f6f, 0x0000006f, 0x0d080805, 0x291b190e, 0x11, 0x1b},
    {0x4a8f0b00, 0x6f6f6f6f, 0x0000006f, 0x0d080805, 0x291b190e, 0x13, 0x1e},
    {0x4b8f0c00, 0x6f6f6f6f, 0x0000006f, 0x0d080805, 0x291b190e, 0x15, 0x22},
    {0x4b8f0c00, 0x6f6f6f6f, 0x0000006f, 0x0d080805, 0x291b190e, 0x15, 0x22},
    {0x4d8f0d00, 0x6f6f6f6f, 0x0000006f, 0x0d080805, 0x291b190e, 0x17, 0x25},
    {0x4e8f0e00, 0x6f6f6f6f, 0x0000006f, 0x190b0c07, 0x2e281e1a, 0x19, 0x29},
    {0x4f8f0f00, 0x6f6f6f6f, 0x0000006f, 0x190b0c07, 0x2e281e1a, 0x1b, 0x2c},
    {0x588f1800, 0x6f6f6f6f, 0x0000006f, 0x190b0c07, 0x2e281e1a, 0x1d, 0x2f},
    {0x588f1900, 0x6f6f6f6f, 0x0000006f, 0x190b0c07, 0x2e281e1a, 0x1f, 0x33},
    {0x598f1900, 0x6f6f6f6f, 0x0000006f, 0x1c0f0f0a, 0x392b291e, 0x21, 0x36},
    {0x5a8f1a00, 0x6f6f6f6f, 0x0000006f, 0x1c0f0f0a, 0x392b291e, 0x23, 0x3a},
    {0x5a8f1a00, 0x6f6f6f6f, 0x0000006f, 0x1c0f0f0a, 0x392b291e, 0x23, 0x3a},
    {0x5a8f1a00, 0x6f6f6f6f, 0x0000006f, 0x1c0f0f0a, 0x392b291e, 0x25, 0x3d},
    {0x5b8f1b00, 0x6f6f6f6f, 0x0000006f, 0x1c0f0f0a, 0x392b291e, 0x27, 0x40},
    {0x5b8f1c00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2a, 0x44},
    {0x5b8f1c00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2a, 0x44},
    {0x5c8f1c00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2c, 0x47},
    {0x5c8f1c00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2c, 0x47},
    {0x5d8f1d00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2e, 0x4a},
    {0x5d8f1d00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x2e, 0x4a},
    {0x5d8f1d00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x30, 0x4e},
    {0x5d8f1d00, 0x6f6f6f6f, 0x0000006f, 0x2819190c, 0x3c2e2b29, 0x30, 0x4e},
    {0x5e8f1e00, 0x6f6f6f6f, 0x0000006f, 0x291b1b0f, 0x3e382e2a, 0x32, 0x51},
    {0x5e8f1f00, 0x6f6f6f6f, 0x0000006f, 0x291b1b0f, 0x3e382e2a, 0x34, 0x55},
    {0x5e8f1f00, 0x6f6f6f6f, 0x0000006f, 0x291b1b0f, 0x3e382e2a, 0x34, 0x55},
    {0x5f8f1f00, 0x6f6f6f6f, 0x0000006f, 0x291b1b0f, 0x3e382e2a, 0x36, 0x58},
    {0x688f2800, 0x6f6f6f6f, 0x0000006f, 0x291b1b0f, 0x3e382e2a, 0x38, 0x5b},
    {0x688f2800, 0x6f6f6f6f, 0x0000006f, 0x2b1d1d18, 0x483a382c, 0x3a, 0x5f},
    {0x688f2800, 0x6f6f6f6f, 0x0000006f, 0x2b1d1d18, 0x483a382c, 0x3c, 0x62},
    {0x688f2900, 0x6f6f6f6f, 0x0000006f, 0x2b1d1d18, 0x483a382c, 0x3e, 0x65},
    {0x698f2900, 0x6f6f6f6f, 0x0000006f, 0x2b1d1d18, 0x483a382c, 0x40, 0x69},
    {0x698f2900, 0x6f6f6f6f, 0x0000006f, 0x2c1f1f19, 0x493b392e, 0x43, 0x6c},
    {0x698f2900, 0x6f6f6f6f, 0x0000006f, 0x2c1f1f19, 0x493b392e, 0x45, 0x70},
    {0x6a8f2a00, 0x6f6f6f6f, 0x0000006f, 0x2c1f1f19, 0x493b392e, 0x47, 0x73},
    {0x6a8f2a00, 0x6f6f6f6f, 0x0000006f, 0x2c1f1f19, 0x493b392e, 0x49, 0x76},
    {0x6a8f2a00, 0x6f6f6f6f, 0x0000006f, 0x2e28281b, 0x4b3d3a38, 0x4b, 0x7a},
    {0x6b8f2b00, 0x6f6f6f6f, 0x0000006f, 0x2e28281b, 0x4b3d3a38, 0x4d, 0x7d},
    {0x6b8f2b00, 0x6f6f6f6f, 0x0000006f, 0x2e28281b, 0x4b3d3a38, 0x4d, 0x7d},
    {0x6b8f2b00, 0x6f6f6f6f, 0x0000006f, 0x2e28281b, 0x4b3d3a38, 0x4f, 0x81},
    {0x6b8f2b00, 0x6f6f6f6f, 0x0000006f, 0x2e28281b, 0x4b3d3a38, 0x51, 0x84},
    {0x6b8f2c00, 0x6f6f6f6f, 0x0000006f, 0x2f29291c, 0x4c3e3b38, 0x53, 0x87},
    {0x6c8f2c00, 0x6f6f6f6f, 0x0000006f, 0x2f29291c, 0x4c3e3b38, 0x55, 0x8b},
    {0x6c8f2c00, 0x6f6f6f6f, 0x0000006f, 0x2f29291c, 0x4c3e3b38, 0x57, 0x8e},
    {0x6c8f2c00, 0x6f6f6f6f, 0x0000006f, 0x2f29291c, 0x4c3e3b38, 0x59, 0x91},
    {0x6d8f2d00, 0x6f6f6f6f, 0x0000006f, 0x382a2a1d, 0x4d483c39, 0x5b, 0x95},
    {0x6d8f2d00, 0x6f6f6f6f, 0x0000006f, 0x382a2a1d, 0x4d483c39, 0x5e, 0x98},
    {0x6d8f2d00, 0x6f6f6f6f, 0x0000006f, 0x382a2a1d, 0x4d483c39, 0x60, 0x9c},
    {0x6d8f2d00, 0x6f6f6f6f, 0x0000006f, 0x382a2a1d, 0x4d483c39, 0x60, 0x9c},
    {0x6d8f2e00, 0x6f6f6f6f, 0x0000006f, 0x382a2a1d, 0x4d483c39, 0x62, 0x9f},
    {0x6e8f2e00, 0x6f6f6f6f, 0x0000006f, 0x392b2b1e, 0x4e483e3a, 0x64, 0xa2},
    {0x6e8f2e00, 0x6f6f6f6f, 0x0000006f, 0x392b2b1e, 0x4e483e3a, 0x66, 0xa6},
    {0x6e8f2e00, 0x6f6f6f6f, 0x0000006f, 0x392b2b1e, 0x4e483e3a, 0x68, 0xa9},
    {0x6f8f2f00, 0x6f6f6f6f, 0x0000006f, 0x392b2b1e, 0x4e483e3a, 0x6a, 0xad},
    {0x6f8f2f00, 0x6f6f6f6f, 0x0000006f, 0x3a2c2c1f, 0x4f493f3b, 0x6c, 0xb0},
    {0x6f8f2f00, 0x6f6f6f6f, 0x0000006f, 0x3a2c2c1f, 0x4f493f3b, 0x6e, 0xb3},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3a2c2c1f, 0x4f493f3b, 0x70, 0xb7},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3a2c2c1f, 0x4f493f3b, 0x72, 0xba},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3b2d2d28, 0x584a483c, 0x74, 0xbd},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3b2d2d28, 0x584a483c, 0x76, 0xc1},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3b2d2d28, 0x584a483c, 0x79, 0xc4},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3b2d2d28, 0x584a483c, 0x7b, 0xc8},
    {0x788f3800, 0x6f6f6f6f, 0x0000006f, 0x3b2e2e29, 0x594b483d, 0x7d, 0xcb},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3b2e2e29, 0x594b483d, 0x7f, 0xce},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3b2e2e29, 0x594b483d, 0x81, 0xd2},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3b2e2e29, 0x594b483d, 0x83, 0xd5},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3c2f2f29, 0x594b493e, 0x85, 0xd9},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3c2f2f29, 0x594b493e, 0x87, 0xdc},
    {0x798f3900, 0x6f6f6f6f, 0x0000006f, 0x3c2f2f29, 0x594b493e, 0x89, 0xdf},
    {0x798f3a00, 0x6f6f6f6f, 0x0000006f, 0x3c2f2f29, 0x594b493e, 0x8b, 0xe3},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3d38382a, 0x5a4c493f, 0x8d, 0xe6},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3d38382a, 0x5a4c493f, 0x8f, 0xe9},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3d38382a, 0x5a4c493f, 0x91, 0xed},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3d38382a, 0x5a4c493f, 0x94, 0xf0},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3e38382b, 0x5b4d4a48, 0x96, 0xf4},
    {0x7a8f3a00, 0x6f6f6f6f, 0x0000006f, 0x3e38382b, 0x5b4d4a48, 0x98, 0xf7},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3e38382b, 0x5b4d4a48, 0x9a, 0xfa},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3e38382b, 0x5b4d4a48, 0x9c, 0xfe},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f38392b, 0x5b4d4b48, 0x9e, 0xff},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f38392b, 0x5b4d4b48, 0x9e, 0xff},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f38392b, 0x5b4d4b48, 0xa0, 0xff},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f38392b, 0x5b4d4b48, 0xa2, 0xff},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f38392b, 0x5b4d4b48, 0xa4, 0xff},
    {0x7b8f3b00, 0x6f6f6f6f, 0x0000006f, 0x3f39392c, 0x5c4e4b48, 0xa6, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x3f39392c, 0x5c4e4b48, 0xa8, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x3f39392c, 0x5c4e4b48, 0xaa, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x3f39392c, 0x5c4e4b48, 0xac, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x48393a2c, 0x5c4f4c49, 0xaf, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x48393a2c, 0x5c4f4c49, 0xb1, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x48393a2c, 0x5c4f4c49, 0xb3, 0xff},
    {0x7c8f3c00, 0x6f6f6f6f, 0x0000006f, 0x48393a2c, 0x5c4f4c49, 0xb5, 0xff},
    {0x7d8f3d00, 0x6f6f6f6f, 0x0000006f, 0x483a3a2d, 0x5d584c49, 0xb7, 0xff},
    {0x7d8f3d00, 0x6f6f6f6f, 0x0000006f, 0x483a3a2d, 0x5d584c49, 0xb9, 0xff},
    {0x7d8f3d00, 0x6f6f6f6f, 0x0000006f, 0x483a3a2d, 0x5d584c49, 0xbd, 0xff},
    {0x7d8f3d00, 0x6f6f6f6f, 0x0000006f, 0x493a3b2e, 0x5e584d4a, 0xc1, 0xff},
    {0x7e8f3e00, 0x6f6f6f6f, 0x0000006f, 0x493a3b2e, 0x5e584d4a, 0xc5, 0xff},
    {0x7e8f3e00, 0x6f6f6f6f, 0x0000006f, 0x493b3b2e, 0x5e584e4a, 0xc8, 0xff},
    {0x7e8f3e00, 0x6f6f6f6f, 0x0000006f, 0x493b3b2e, 0x5e584e4a, 0xcc, 0xff},
    {0x7e8f3e00, 0x6f6f6f6f, 0x0000006f, 0x493b3c2f, 0x5f594e4b, 0xd0, 0xff},
    {0x7f8f3f00, 0x6f6f6f6f, 0x0000006f, 0x493b3c2f, 0x5f594e4b, 0xd2, 0xff},
    {0x7f8f3f00, 0x6f6f6f6f, 0x0000006f, 0x493b3c2f, 0x5f594e4b, 0xd4, 0xff},
    {0x7f8f3f00, 0x6f6f6f6f, 0x0000006f, 0x4a3c3c2f, 0x5f594f4b, 0xd8, 0xff},
    {0x7f8f3f00, 0x6f6f6f6f, 0x0000006f, 0x4a3c3c2f, 0x5f594f4b, 0xdc, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4a3c3d38, 0x68594f4c, 0xe0, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4a3c3d38, 0x68594f4c, 0xe5, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4b3d3d38, 0x685a584c, 0xe9, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4b3d3d38, 0x685a584c, 0xed, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4b3d3e38, 0x685a584c, 0xf1, 0xff},
    {0x888f4800, 0x6f6f6f6f, 0x0000006f, 0x4b3d3e38, 0x685a584c, 0xf5, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4b3e3e39, 0x695b584d, 0xfe, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4c3e3e39, 0x695b594d, 0xff, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4c3e3e39, 0x695b594d, 0xff, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4c3f3f39, 0x695b594e, 0xff, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4c3f3f39, 0x695b594e, 0xff, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4d3f3f3a, 0x6a5c594e, 0xff, 0xff},
    {0x898f4900, 0x6f6f6f6f, 0x0000006f, 0x4d3f3f3a, 0x6a5c594e, 0xff, 0xff},
    {0x8a8f4a00, 0x6f6f6f6f, 0x0000006f, 0x4d48483a, 0x6a5c594f, 0xff, 0xff},
    {0x8a8f4a00, 0x6f6f6f6f, 0x0000006f, 0x4d48483a, 0x6a5c594f, 0xff, 0xff},
    {0x8a8f4a00, 0x6f6f6f6f, 0x0000006f, 0x4d48483a, 0x6a5c5a4f, 0xff, 0xff},
    {0x8a8f4a00, 0x6f6f6f6f, 0x0000006f, 0x4d48483a, 0x6a5c5a4f, 0xff, 0xff},
    {0x8a8f4a00, 0x6f6f6f6f, 0x0000006f, 0x4e48483a, 0x6a5d5a58, 0xff, 0xff},
    {0x8b8f4b00, 0x6f6f6f6f, 0x0000006f, 0x4e48483b, 0x6b5d5a58, 0xff, 0xff},
    {0x8b8f4b00, 0x6f6f6f6f, 0x0000006f, 0x4e48483b, 0x6b5d5a58, 0xff, 0xff},
    {0x8b8f4b00, 0x6f6f6f6f, 0x0000006f, 0x4f48493b, 0x6b5d5b58, 0xff, 0xff},
    {0x8b8f4b00, 0x6f6f6f6f, 0x0000006f, 0x4f49493b, 0x6b5e5b58, 0xff, 0xff}
};

static const unsigned int single_su_vp8[56] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000
};

static const unsigned char full_spiral_48x40_vp8[56] = {
    // L -> U -> R -> D
    0x0F,
    0xF0,
    0x01, 0x01,
    0x10, 0x10,
    0x0F, 0x0F, 0x0F,
    0xF0, 0xF0, 0xF0,
    0x01, 0x01, 0x01, 0x01,
    0x10, 0x10, 0x10, 0x10,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10,       // The last 0x10 steps outside the search window.
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // These are outside the search window.
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0
};

static const unsigned char raster_scan_48x40_vp8[56] = {
    0x11, 0x01, 0x01, 0x01,
    0x11, 0x01, 0x01, 0x01,
    0x11, 0x01, 0x01, 0x01,
    0x11, 0x01, 0x01, 0x01,
    0x11, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static const unsigned char diamond_vp8[56] = {
    0x0F, 0xF1, 0x0F, 0x12,//5
    0x0D, 0xE2, 0x22, 0x1E,//9
    0x10, 0xFF, 0xE2, 0x20,//13
    0xFC, 0x06, 0xDD,//16
    0x2E, 0xF1, 0x3F, 0xD3, 0x11, 0x3D, 0xF3, 0x1F,//24
    0xEB, 0xF1, 0xF1, 0xF1,//28
    0x4E, 0x11, 0x12, 0xF2, 0xF1,//33
    0xE0, 0xFF, 0xFF, 0x0D, 0x1F, 0x1F,//39
    0x20, 0x11, 0xCF, 0xF1, 0x05, 0x11,//45
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//51
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned short
mv_ref_cost_context_vp8[6][4][2] = {
    {   {1328, 10},
        {2047, 1},
        {2047, 1},
        {214, 304},
    },
    {   {1072, 21},
        {979, 27},
        {1072, 21},
        {321, 201},
    },
    {   {235, 278},
        {511, 107},
        {553, 93},
        {488, 115},
    },
    {   {534, 99},
        {560, 92},
        {255, 257},
        {505, 109},
    },
    {   {174, 361},
        {238, 275},
        {255, 257},
        {744, 53},
    },
    {   {32, 922},
        {113, 494},
        {255, 257},
        {816, 43},
    },
};

static const unsigned int
new_mv_skip_threshold_vp8[NUM_QP_VP8] = {
    111, 120, 129, 137, 146, 155, 163, 172, 180, 189, 198, 206, 215, 224, 232, 241,
    249, 258, 267, 275, 284, 293, 301, 310, 318, 327, 336, 344, 353, 362, 370, 379,
    387, 396, 405, 413, 422, 431, 439, 448, 456, 465, 474, 482, 491, 500, 508, 517,
    525, 534, 543, 551, 560, 569, 577, 586, 594, 603, 612, 620, 629, 638, 646, 655,
    663, 672, 681, 689, 698, 707, 715, 724, 733, 741, 750, 758, 767, 776, 784, 793,
    802, 810, 819, 827, 836, 845, 853, 862, 871, 879, 888, 896, 905, 914, 922, 931,
    940, 948, 957, 965, 974, 983, 991, 1000, 1009, 1017, 1026, 1034, 1043, 1052, 1060, 1069,
    1078, 1086, 1095, 1103, 1112, 1121, 1129, 1138, 1147, 1155, 1164, 1172, 1181, 1190, 1198, 1208
};

static const unsigned short
mb_mode_cost_luma_vp8[10] = {
    657,    869,    915,    917,    208,    0,      0,      0,      0,      0
};


static const unsigned short
block_mode_cost_vp8[10][10][10] = {
    {
        {37,  1725,  1868,  1151,  1622,  2096,  2011,  1770,  2218,  2128  },
        {139,  759,  1683,  911,  1455,  1846,  1570,  1295,  1792,  1648   },
        {560,  1383,  408,  639,  1612,  1174,  1562,  1736,  847,  991     },
        {191,  1293,  1299,  466,  1774,  1840,  1784,  1691,  1698,  1505  },
        {211,  1624,  1294,  779,  714,  1622,  2222,  1554,  1706,  903    },
        {297,  1259,  1098,  1062,  1583,  618,  1053,  1889,  851,  1127   },
        {275,  703,  1356,  1111,  1597,  1075,  656,  1529,  1531,  1275   },
        {150,  1046,  1760,  1039,  1353,  1981,  2174,  728,  1730,  1379  },
        {516,  1414,  741,  1045,  1495,  738,  1288,  1619,  442,  1200    },
        {424,  1365,  706,  825,  1197,  1453,  1191,  1462,  1186,  519    },
    },
    {
        {393,  515,  1491,  549,  1598,  1524,  964,  1126,  1651,  2172    },
        {693,  237,  1954,  641,  1525,  2073,  1183,  971,  1973,  2235    },
        {560,  739,  855,  836,  1224,  1115,  966,  839,  1076,  767       },
        {657,  368,  1406,  425,  1672,  1853,  1210,  1125,  1969,  1542   },
        {321,  1056,  1776,  774,  803,  3311,  1265,  1177,  1366,  636    },
        {693,  510,  949,  877,  1049,  658,  882,  1178,  1515,  1111      },
        {744,  377,  1278,  958,  1576,  1168,  477,  1146,  1838,  1501    },
        {488,  477,  1767,  973,  1107,  1511,  1773,  486,  1527,  1449    },
        {744,  1004,  695,  1012,  1326,  834,  1215,  774,  724,  704      },
        {522,  567,  1036,  1082,  1039,  1333,  873,  1135,  1189,  677    },
    },
    {
        {103,  1441,  1000,  864,  1513,  1928,  1832,  1916,  1663,  1567  },
        {304,  872,  1100,  515,  1416,  1417,  3463,  1051,  1305,  1227   },
        {684,  2176,  242,  729,  1867,  1496,  2056,  1544,  1038,  930    },
        {534,  1198,  669,  300,  1805,  1377,  2165,  1894,  1249,  1153   },
        {346,  1602,  1178,  612,  997,  3381,  1335,  1328,  997,  646     },
        {393,  1027,  649,  813,  1276,  945,  1545,  1278,  875,  1031     },
        {528,  996,  930,  617,  1086,  1190,  621,  2760,  787,  1347      },
        {216,  873,  1595,  738,  1339,  3896,  3898,  743,  1343,  1605    },
        {675,  1580,  543,  749,  1859,  1245,  1589,  2377,  384,  1075    },
        {594,  1163,  415,  684,  1474,  1080,  1491,  1478,  1077,  801    },
    },
    {
        {238,  1131,  1483,  398,  1510,  1651,  1495,  1545,  1970,  2090  },
        {499,  456,  1499,  449,  1558,  1691,  1272,  969,  2114,  2116    },
        {675,  1386,  318,  645,  1449,  1588,  1666,  1925,  979,  859     },
        {467,  957,  1223,  238,  1825,  1704,  1608,  1560,  1665,  1376   },
        {331,  1460,  1238,  627,  787,  1882,  3928,  1544,  1897,  579    },
        {457,  1038,  903,  784,  1158,  725,  955,  1517,  842,  1016      },
        {505,  497,  1131,  812,  1508,  1206,  703,  1072,  1254,  1256    },
        {397,  741,  1336,  642,  1506,  1852,  1340,  599,  1854,  1000    },
        {625,  1212,  597,  750,  1291,  1057,  1401,  1401,  527,  954     },
        {499,  1041,  654,  752,  1299,  1217,  1605,  1424,  1377,  505    },
    },
    {
        {263,  1094,  1218,  602,  938,  1487,  1231,  1016,  1724,  1448   },
        {452,  535,  1728,  562,  1008,  1471,  1473,  873,  3182,  1136    },
        {553,  1570,  935,  1093,  826,  1339,  879,  1007,  1006,  476     },
        {365,  900,  1050,  582,  866,  1398,  1236,  1123,  1608,  1039    },
        {294,  2044,  1790,  1143,  430,  1642,  3688,  1549,  2080,  704   },
        {703,  1210,  958,  815,  1211,  960,  623,  2455,  815,  559       },
        {675,  574,  862,  1261,  866,  864,  761,  1267,  1014,  936       },
        {342,  1254,  1857,  989,  612,  1856,  1858,  553,  1840,  1037    },
        {553,  1316,  811,  1072,  1068,  728,  1328,  1317,  1064,  475    },
        {288,  1303,  1167,  1167,  823,  1634,  1636,  2497,  1294,  491   },
    },
    {
        {227,  1059,  1369,  1066,  1505,  740,  970,  1511,  972,  1775    },
        {516,  587,  1033,  646,  1188,  748,  978,  1445,  1294,  1450     },
        {684,  1048,  663,  747,  1126,  826,  1386,  1128,  635,  924      },
        {494,  814,  933,  510,  1606,  951,  878,  1344,  1031,  1347      },
        {553,  1071,  1327,  726,  809,  3376,  1330,  1324,  1062,  407    },
        {625,  1120,  988,  1121,  1197,  347,  1064,  1308,  862,  1206    },
        {633,  853,  1657,  1073,  1662,  634,  460,  1405,  811,  1155     },
        {505,  621,  1394,  876,  1394,  876,  878,  795,  878,  1399       },
        {684,  1302,  968,  1704,  1280,  561,  972,  1713,  387,  1104     },
        {397,  1447,  1060,  867,  957,  1058,  749,  1475,  1210,  660     },
    },
    {
        {331,  933,  1647,  761,  1647,  998,  513,  1402,  1461,  2219     },
        {573,  485,  1968,  641,  1570,  1198,  588,  1086,  1382,  1982    },
        {790,  942,  570,  790,  1607,  1005,  938,  1193,  714,  751       },
        {511,  745,  1152,  492,  1878,  1206,  596,  1867,  1617,  1157    },
        {452,  1308,  896,  896,  451,  1308,  3354,  1301,  1306,  794     },
        {693,  670,  1072,  1020,  1687,  566,  488,  1432,  1096,  3142    },
        {778,  566,  1993,  1283,  3139,  1251,  227,  1378,  1784,  1447   },
        {393,  937,  1091,  934,  939,  1348,  1092,  579,  1351,  1095     },
        {560,  1013,  1007,  1014,  1011,  644,  1165,  1155,  605,  1016   },
        {567,  627,  997,  793,  2562,  998,  849,  1260,  922,  748        },
    },
    {
        {338,  762,  1868,  717,  1247,  1757,  1263,  535,  1751,  2162    },
        {488,  442,  3235,  756,  1658,  1814,  1264,  528,  1857,  2119    },
        {522,  1087,  840,  1103,  843,  1354,  1098,  888,  946,  588      },
        {483,  688,  1502,  651,  1213,  1446,  1397,  491,  1908,  1253    },
        {452,  1386,  1910,  1175,  298,  1507,  3553,  930,  1904,  905    },
        {713,  839,  716,  715,  932,  719,  931,  848,  3088,  1042        },
        {516,  495,  1331,  1340,  1331,  1069,  665,  702,  1593,  1337    },
        {401,  977,  2167,  1537,  1069,  1764,  3810,  259,  3624,  1578   },
        {560,  1104,  601,  1371,  965,  658,  2704,  779,  967,  969       },
        {547,  1057,  801,  1141,  1133,  1397,  937,  605,  1252,  631     },
    },
    {
        {163,  1240,  925,  983,  1653,  1321,  1353,  1566,  946,  1601    },
        {401,  726,  758,  836,  1241,  926,  1656,  795,  1394,  1396      },
        {905,  1073,  366,  876,  1436,  1576,  1732,  2432,  459,  1019    },
        {594,  922,  835,  417,  1387,  1124,  1098,  2042,  843,  1023     },
        {415,  1262,  860,  1274,  758,  1272,  3318,  1010,  1276,  503    },
        {641,  1018,  1020,  1095,  1619,  667,  1371,  2348,  397,  849    },
        {560,  817,  903,  1014,  1420,  695,  756,  904,  821,  1421       },
        {406,  596,  1001,  993,  1257,  1258,  1260,  746,  1002,  1264    },
        {979,  1371,  780,  1188,  1693,  1024,  1286,  1699,  183,  1405   },
        {733,  1292,  458,  884,  1554,  889,  1151,  1286,  738,  740      },
    },
    {
        {109,  1377,  1177,  933,  1140,  1928,  1639,  1705,  1861,  1292  },
        {342,  570,  1081,  638,  1154,  1231,  1339,  1342,  1750,  1494   },
        {560,  1203,  345,  767,  1325,  1681,  1425,  1905,  1205,  786    },
        {406,  1027,  1011,  410,  1306,  1901,  1389,  1636,  1493,  776   },
        {206,  1329,  1337,  1037,  802,  1600,  3646,  1451,  1603,  693   },
        {472,  1167,  758,  911,  1424,  703,  2749,  1428,  703,  764      },
        {342,  780,  1139,  889,  1290,  1139,  781,  1544,  957,  1042     },
        {227,  888,  1039,  929,  988,  3753,  1707,  818,  1710,  1306     },
        {767,  1055,  627,  725,  1312,  980,  1065,  1324,  599,  811      },
        {304,  1372,  888,  1173,  979,  1578,  1580,  1974,  1318,  482    },
    }
};

static const unsigned char
brc_qpadjustment_distthreshold_maxframethreshold_distqpadjustment_ipb_vp8[576] = {
    0x01, 0x03, 0x05, 0x07, 0x09, 0x01, 0x02, 0x03, 0x05, 0x07, 0x00, 0x00, 0x01, 0x02, 0x04, 0x00,
    0x00, 0x00, 0x01, 0x02, 0xff, 0x00, 0x00, 0x00, 0x01, 0xfd, 0xfe, 0xff, 0x00, 0x00, 0xfb, 0xfc,
    0xfe, 0xff, 0x00, 0xf9, 0xfa, 0xfc, 0xfe, 0xff, 0xf7, 0xf9, 0xfb, 0xfe, 0xff, 0x00, 0x04, 0x1e,
    0x3c, 0x50, 0x78, 0x8c, 0xc8, 0xff, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x05, 0x08, 0x0a, 0x01, 0x02, 0x04, 0x06, 0x08, 0x00, 0x01, 0x02, 0x04, 0x06, 0x00,
    0x00, 0x00, 0x01, 0x02, 0xff, 0x00, 0x00, 0x00, 0x01, 0xfe, 0xff, 0xff, 0x00, 0x00, 0xfd, 0xfe,
    0xff, 0xff, 0x00, 0xfb, 0xfd, 0xfe, 0xff, 0x00, 0xf9, 0xfa, 0xfc, 0xfe, 0xff, 0x00, 0x04, 0x1e,
    0x3c, 0x50, 0x78, 0x8c, 0xc8, 0xff, 0x04, 0x05, 0x06, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x05, 0x08, 0x0a, 0x01, 0x02, 0x04, 0x06, 0x08, 0x00, 0x01, 0x02, 0x04, 0x06, 0x00,
    0x00, 0x00, 0x01, 0x02, 0xff, 0x00, 0x00, 0x00, 0x01, 0xfe, 0xff, 0xff, 0x00, 0x00, 0xfd, 0xfe,
    0xff, 0xff, 0x00, 0xfb, 0xfd, 0xfe, 0xff, 0x00, 0xf9, 0xfa, 0xfc, 0xfe, 0xff, 0x00, 0x02, 0x14,
    0x28, 0x46, 0x82, 0xa0, 0xc8, 0xff, 0x04, 0x05, 0x06, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x06, 0x08, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05,
    0x07, 0x09, 0xff, 0x00, 0x00, 0x00, 0x00, 0x03, 0x04, 0x06, 0x07, 0xfe, 0xff, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x03, 0x05, 0xfd, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0xfc, 0xfe, 0xff,
    0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0xfb, 0xfd, 0xfe, 0xff, 0x00, 0x00, 0x01, 0x03, 0x05, 0xfa,
    0xfc, 0xfe, 0xff, 0x00, 0x00, 0x01, 0x03, 0x05, 0xfa, 0xfc, 0xfe, 0xff, 0x00, 0x00, 0x01, 0x03,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x07, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05,
    0x06, 0x08, 0xff, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x07, 0x08, 0xfe, 0xff, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x05, 0x06, 0xfd, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01, 0x04, 0x05, 0xfc, 0xfe, 0xff,
    0x00, 0x00, 0x00, 0x01, 0x04, 0x05, 0xfc, 0xfe, 0xff, 0xff, 0x00, 0x00, 0x00, 0x04, 0x05, 0xfc,
    0xfd, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01, 0x05, 0xfb, 0xfc, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x07, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05,
    0x06, 0x08, 0xff, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x07, 0x08, 0xfe, 0xff, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x05, 0x06, 0xfd, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01, 0x04, 0x05, 0xfc, 0xfe, 0xff,
    0x00, 0x00, 0x00, 0x01, 0x04, 0x05, 0xfc, 0xfe, 0xff, 0xff, 0x00, 0x00, 0x00, 0x04, 0x05, 0xfc,
    0xfd, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01, 0x05, 0xfb, 0xfc, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x01,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char
brc_iframe_cost_vp8[128][4] = {
    { 0x5, 0x5, 0x8, 0x8 },
    { 0xa, 0xa, 0xd, 0xd },
    { 0xd, 0xf, 0xf, 0x19 },
    { 0x19, 0x1a, 0x1a, 0x1a },
    { 0x1b, 0x1b, 0x1d, 0x1d },
    { 0x1d, 0x1d, 0x1e, 0x1e },
    { 0x1e, 0x1e, 0x1f, 0x1f },
    { 0x1f, 0x28, 0x28, 0x29 },
    { 0x29, 0x29, 0x29, 0x2a },
    { 0x2a, 0x2b, 0x2b, 0x2b },
    { 0x2b, 0x2b, 0x2c, 0x2c },
    { 0x2c, 0x2c, 0x2d, 0x2d },
    { 0x2e, 0x2e, 0x2e, 0x2e },
    { 0x2e, 0x2f, 0x2f, 0x38 },
    { 0x38, 0x38, 0x38, 0x38 },
    { 0x38, 0x39, 0x39, 0x39 },
    { 0x39, 0x39, 0x39, 0x3a },
    { 0x3a, 0x3a, 0x3a, 0x3a },
    { 0x3a, 0x3a, 0x3a, 0x3b },
    { 0x3b, 0x3b, 0x3b, 0x3b },
    { 0x3b, 0x3c, 0x3c, 0x3c },
    { 0x3c, 0x3c, 0x3c, 0x3c },
    { 0x3d, 0x3d, 0x3d, 0x3d },
    { 0x3d, 0x3d, 0x3e, 0x3e },
    { 0x3e, 0x3e, 0x3f, 0x3f },
    { 0x3f, 0x48, 0x48, 0x48 },
    { 0x48, 0x48, 0x48, 0x48 },
    { 0x49, 0x49, 0x49, 0x49 },
    { 0x49, 0x49, 0x4a, 0x4a },
    { 0x4a, 0x4a, 0x4a, 0x4a },
    { 0x4b, 0x4b, 0x4b, 0x4b },
    { 0x4b, 0x4c, 0x4c, 0x4c },
    { 0x1f, 0x1f, 0x2b, 0x2b },
    { 0x2f, 0x2f, 0x39, 0x39 },
    { 0x39, 0x3b, 0x3b, 0x3d },
    { 0x3d, 0x3f, 0x3f, 0x3f },
    { 0x48, 0x48, 0x49, 0x49 },
    { 0x49, 0x49, 0x4a, 0x4a },
    { 0x4a, 0x4a, 0x4b, 0x4b },
    { 0x4b, 0x4c, 0x4c, 0x4d },
    { 0x4d, 0x4e, 0x4e, 0x4f },
    { 0x4f, 0x58, 0x58, 0x58 },
    { 0x58, 0x58, 0x58, 0x58 },
    { 0x59, 0x59, 0x59, 0x59 },
    { 0x5a, 0x5a, 0x5a, 0x5a },
    { 0x5a, 0x5b, 0x5b, 0x5b },
    { 0x5b, 0x5c, 0x5c, 0x5c },
    { 0x5c, 0x5d, 0x5d, 0x5d },
    { 0x5d, 0x5e, 0x5e, 0x5e },
    { 0x5e, 0x5f, 0x5f, 0x5f },
    { 0x5f, 0x68, 0x68, 0x68 },
    { 0x68, 0x68, 0x68, 0x68 },
    { 0x68, 0x68, 0x68, 0x68 },
    { 0x69, 0x69, 0x69, 0x69 },
    { 0x69, 0x69, 0x69, 0x69 },
    { 0x6a, 0x6a, 0x6a, 0x6a },
    { 0x6a, 0x6a, 0x6b, 0x6b },
    { 0x6b, 0x6b, 0x6b, 0x6c },
    { 0x6c, 0x6c, 0x6c, 0x6c },
    { 0x6d, 0x6d, 0x6d, 0x6d },
    { 0x6e, 0x6e, 0x6e, 0x6f },
    { 0x6f, 0x6f, 0x6f, 0x6f },
    { 0x78, 0x78, 0x78, 0x78 },
    { 0x78, 0x78, 0x79, 0x79 },
    { 0x2, 0x2, 0x3, 0x3 },
    { 0x4, 0x4, 0x5, 0x5 },
    { 0x5, 0x6, 0x6, 0x7 },
    { 0x7, 0x8, 0x8, 0x8 },
    { 0x9, 0x9, 0x9, 0x9 },
    { 0x9, 0x9, 0xa, 0xa },
    { 0xa, 0xa, 0xb, 0xb },
    { 0xb, 0xc, 0xc, 0xd },
    { 0xd, 0xe, 0xe, 0xf },
    { 0xf, 0x10, 0x10, 0x11 },
    { 0x11, 0x11, 0x12, 0x12 },
    { 0x13, 0x13, 0x14, 0x14 },
    { 0x15, 0x15, 0x16, 0x16 },
    { 0x16, 0x17, 0x17, 0x18 },
    { 0x18, 0x19, 0x19, 0x1a },
    { 0x1a, 0x1a, 0x1a, 0x1b },
    { 0x1b, 0x1c, 0x1c, 0x1d },
    { 0x1d, 0x1e, 0x1e, 0x1f },
    { 0x1f, 0x20, 0x20, 0x21 },
    { 0x21, 0x22, 0x22, 0x23 },
    { 0x23, 0x24, 0x24, 0x24 },
    { 0x25, 0x25, 0x26, 0x26 },
    { 0x27, 0x27, 0x28, 0x28 },
    { 0x29, 0x29, 0x2a, 0x2a },
    { 0x2b, 0x2b, 0x2c, 0x2d },
    { 0x2e, 0x2f, 0x2f, 0x30 },
    { 0x31, 0x32, 0x33, 0x34 },
    { 0x35, 0x36, 0x37, 0x38 },
    { 0x3a, 0x3b, 0x3c, 0x3d },
    { 0x3d, 0x3e, 0x3f, 0x40 },
    { 0x41, 0x42, 0x43, 0x44 },
    { 0x46, 0x47, 0x49, 0x4a },
    { 0x9, 0x9, 0xe, 0xe },
    { 0x12, 0x12, 0x17, 0x17 },
    { 0x17, 0x1b, 0x1b, 0x20 },
    { 0x20, 0x24, 0x24, 0x24 },
    { 0x29, 0x29, 0x2d, 0x2d },
    { 0x2d, 0x2d, 0x32, 0x32 },
    { 0x32, 0x32, 0x36, 0x36 },
    { 0x36, 0x3b, 0x3b, 0x3f },
    { 0x3f, 0x44, 0x44, 0x48 },
    { 0x48, 0x4d, 0x4d, 0x51 },
    { 0x51, 0x51, 0x56, 0x56 },
    { 0x5a, 0x5a, 0x5f, 0x5f },
    { 0x63, 0x63, 0x68, 0x68 },
    { 0x68, 0x6c, 0x6c, 0x71 },
    { 0x71, 0x76, 0x76, 0x7a },
    { 0x7a, 0x7f, 0x7f, 0x83 },
    { 0x83, 0x88, 0x88, 0x8c },
    { 0x8c, 0x91, 0x91, 0x95 },
    { 0x95, 0x9a, 0x9a, 0x9e },
    { 0x9e, 0xa3, 0xa3, 0xa7 },
    { 0xa7, 0xac, 0xac, 0xac },
    { 0xb0, 0xb0, 0xb5, 0xb5 },
    { 0xb9, 0xb9, 0xbe, 0xbe },
    { 0xc2, 0xc2, 0xc7, 0xc7 },
    { 0xcb, 0xd0, 0xd4, 0xd9 },
    { 0xdd, 0xe2, 0xe2, 0xe6 },
    { 0xeb, 0xf0, 0xf4, 0xf9 },
    { 0xfd, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0xff },
};

static const unsigned int
brc_pframe_cost_vp8[256] = {
    0x06040402,
    0x06040402,
    0x06040402,
    0x06040402,
    0x0d080805,
    0x0d080805,
    0x0d080805,
    0x0d080805,
    0x0d080805,
    0x190b0c07,
    0x190b0c07,
    0x190b0c07,
    0x190b0c07,
    0x1c0f0f0a,
    0x1c0f0f0a,
    0x1c0f0f0a,
    0x1c0f0f0a,
    0x1c0f0f0a,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x2819190c,
    0x291b1b0f,
    0x291b1b0f,
    0x291b1b0f,
    0x291b1b0f,
    0x291b1b0f,
    0x2b1d1d18,
    0x2b1d1d18,
    0x2b1d1d18,
    0x2b1d1d18,
    0x2c1f1f19,
    0x2c1f1f19,
    0x2c1f1f19,
    0x2c1f1f19,
    0x2e28281b,
    0x2e28281b,
    0x2e28281b,
    0x2e28281b,
    0x2e28281b,
    0x2f29291c,
    0x2f29291c,
    0x2f29291c,
    0x2f29291c,
    0x382a2a1d,
    0x382a2a1d,
    0x382a2a1d,
    0x382a2a1d,
    0x382a2a1d,
    0x392b2b1e,
    0x392b2b1e,
    0x392b2b1e,
    0x392b2b1e,
    0x3a2c2c1f,
    0x3a2c2c1f,
    0x3a2c2c1f,
    0x3a2c2c1f,
    0x3b2d2d28,
    0x3b2d2d28,
    0x3b2d2d28,
    0x3b2d2d28,
    0x3b2e2e29,
    0x3b2e2e29,
    0x3b2e2e29,
    0x3b2e2e29,
    0x3c2f2f29,
    0x3c2f2f29,
    0x3c2f2f29,
    0x3c2f2f29,
    0x3d38382a,
    0x3d38382a,
    0x3d38382a,
    0x3d38382a,
    0x3e38382b,
    0x3e38382b,
    0x3e38382b,
    0x3e38382b,
    0x3f38392b,
    0x3f38392b,
    0x3f38392b,
    0x3f38392b,
    0x3f38392b,
    0x3f39392c,
    0x3f39392c,
    0x3f39392c,
    0x3f39392c,
    0x48393a2c,
    0x48393a2c,
    0x48393a2c,
    0x48393a2c,
    0x483a3a2d,
    0x483a3a2d,
    0x483a3a2d,
    0x493a3b2e,
    0x493a3b2e,
    0x493b3b2e,
    0x493b3b2e,
    0x493b3c2f,
    0x493b3c2f,
    0x493b3c2f,
    0x4a3c3c2f,
    0x4a3c3c2f,
    0x4a3c3d38,
    0x4a3c3d38,
    0x4b3d3d38,
    0x4b3d3d38,
    0x4b3d3e38,
    0x4b3d3e38,
    0x4b3e3e39,
    0x4c3e3e39,
    0x4c3e3e39,
    0x4c3f3f39,
    0x4c3f3f39,
    0x4d3f3f3a,
    0x4d3f3f3a,
    0x4d48483a,
    0x4d48483a,
    0x4d48483a,
    0x4d48483a,
    0x4e48483a,
    0x4e48483b,
    0x4e48483b,
    0x4f48493b,
    0x4f49493b,
    0x1a0c0907,
    0x1a0c0907,
    0x1a0c0907,
    0x1a0c0907,
    0x291b190e,
    0x291b190e,
    0x291b190e,
    0x291b190e,
    0x291b190e,
    0x2e281e1a,
    0x2e281e1a,
    0x2e281e1a,
    0x2e281e1a,
    0x392b291e,
    0x392b291e,
    0x392b291e,
    0x392b291e,
    0x392b291e,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3c2e2b29,
    0x3e382e2a,
    0x3e382e2a,
    0x3e382e2a,
    0x3e382e2a,
    0x3e382e2a,
    0x483a382c,
    0x483a382c,
    0x483a382c,
    0x483a382c,
    0x493b392e,
    0x493b392e,
    0x493b392e,
    0x493b392e,
    0x4b3d3a38,
    0x4b3d3a38,
    0x4b3d3a38,
    0x4b3d3a38,
    0x4b3d3a38,
    0x4c3e3b38,
    0x4c3e3b38,
    0x4c3e3b38,
    0x4c3e3b38,
    0x4d483c39,
    0x4d483c39,
    0x4d483c39,
    0x4d483c39,
    0x4d483c39,
    0x4e483e3a,
    0x4e483e3a,
    0x4e483e3a,
    0x4e483e3a,
    0x4f493f3b,
    0x4f493f3b,
    0x4f493f3b,
    0x4f493f3b,
    0x584a483c,
    0x584a483c,
    0x584a483c,
    0x584a483c,
    0x594b483d,
    0x594b483d,
    0x594b483d,
    0x594b483d,
    0x594b493e,
    0x594b493e,
    0x594b493e,
    0x594b493e,
    0x5a4c493f,
    0x5a4c493f,
    0x5a4c493f,
    0x5a4c493f,
    0x5b4d4a48,
    0x5b4d4a48,
    0x5b4d4a48,
    0x5b4d4a48,
    0x5b4d4b48,
    0x5b4d4b48,
    0x5b4d4b48,
    0x5b4d4b48,
    0x5b4d4b48,
    0x5c4e4b48,
    0x5c4e4b48,
    0x5c4e4b48,
    0x5c4e4b48,
    0x5c4f4c49,
    0x5c4f4c49,
    0x5c4f4c49,
    0x5c4f4c49,
    0x5d584c49,
    0x5d584c49,
    0x5d584c49,
    0x5e584d4a,
    0x5e584d4a,
    0x5e584e4a,
    0x5e584e4a,
    0x5f594e4b,
    0x5f594e4b,
    0x5f594e4b,
    0x5f594f4b,
    0x5f594f4b,
    0x68594f4c,
    0x68594f4c,
    0x685a584c,
    0x685a584c,
    0x685a584c,
    0x685a584c,
    0x695b584d,
    0x695b594d,
    0x695b594d,
    0x695b594e,
    0x695b594e,
    0x6a5c594e,
    0x6a5c594e,
    0x6a5c594f,
    0x6a5c594f,
    0x6a5c5a4f,
    0x6a5c5a4f,
    0x6a5d5a58,
    0x6b5d5a58,
    0x6b5d5a58,
    0x6b5d5b58,
    0x6b5e5b58,
};

static const unsigned short
brc_skip_mv_threshold_vp8[256] = {
    111,  120,  129,  137,  146,  155,  163,  172,  180,  189,  198,  206,  215,  224,  232,  241,
    249,  258,  267,  275,  284,  293,  301,  310,  318,  327,  336,  344,  353,  362,  370,  379,
    387,  396,  405,  413,  422,  431,  439,  448,  456,  465,  474,  482,  491,  500,  508,  517,
    525,  534,  543,  551,  560,  569,  577,  586,  594,  603,  612,  620,  629,  638,  646,  655,
    663,  672,  681,  689,  698,  707,  715,  724,  733,  741,  750,  758,  767,  776,  784,  793,
    802,  810,  819,  827,  836,  845,  853,  862,  871,  879,  888,  896,  905,  914,  922,  931,
    940,  948,  957,  965,  974,  983,  991, 1000, 1009, 1017, 1026, 1034, 1043, 1052, 1060, 1069,
    1078, 1086, 1095, 1103, 1112, 1121, 1129, 1138, 1147, 1155, 1164, 1172, 1181, 1190, 1198, 1208
};

void
i965_encoder_vp8_check_motion_estimation(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    if (vp8_context->down_scaled_width_4x < vp8_context->min_scaled_dimension  ||
        vp8_context->down_scaled_width_in_mb4x < vp8_context->min_scaled_dimension_in_mbs ||
        vp8_context->down_scaled_height_4x < vp8_context->min_scaled_dimension ||
        vp8_context->down_scaled_height_in_mb4x < vp8_context->min_scaled_dimension_in_mbs) {

        vp8_context->hme_16x_supported = 0;

        if (vp8_context->down_scaled_width_4x < vp8_context->min_scaled_dimension  ||
            vp8_context->down_scaled_width_in_mb4x < vp8_context->min_scaled_dimension_in_mbs) {

            vp8_context->down_scaled_width_4x = vp8_context->min_scaled_dimension;
            vp8_context->down_scaled_width_in_mb4x = vp8_context->min_scaled_dimension_in_mbs;
        }

        if (vp8_context->down_scaled_height_4x < vp8_context->min_scaled_dimension ||
            vp8_context->down_scaled_height_in_mb4x < vp8_context->min_scaled_dimension_in_mbs) {

            vp8_context->down_scaled_height_4x = vp8_context->min_scaled_dimension;
            vp8_context->down_scaled_height_in_mb4x = vp8_context->min_scaled_dimension_in_mbs;
        }
    } else if (vp8_context->down_scaled_width_16x < vp8_context->min_scaled_dimension ||
               vp8_context->down_scaled_width_in_mb16x < vp8_context->min_scaled_dimension_in_mbs ||
               vp8_context->down_scaled_height_16x < vp8_context->min_scaled_dimension ||
               vp8_context->down_scaled_height_in_mb16x < vp8_context->min_scaled_dimension_in_mbs) {

        if (vp8_context->down_scaled_width_16x < vp8_context->min_scaled_dimension ||
            vp8_context->down_scaled_width_in_mb16x < vp8_context->min_scaled_dimension_in_mbs) {

            vp8_context->down_scaled_width_16x = vp8_context->min_scaled_dimension;
            vp8_context->down_scaled_width_in_mb16x = vp8_context->min_scaled_dimension_in_mbs;
        }

        if (vp8_context->down_scaled_height_16x < vp8_context->min_scaled_dimension ||
            vp8_context->down_scaled_height_in_mb16x < vp8_context->min_scaled_dimension_in_mbs) {

            vp8_context->down_scaled_height_16x = vp8_context->min_scaled_dimension;
            vp8_context->down_scaled_height_in_mb16x = vp8_context->min_scaled_dimension_in_mbs;
        }
    }
}

static void
i965_encoder_vp8_free_surfaces(void **data)
{
    struct i965_encoder_vp8_surface *vp8_surface;

    if (!data || !(*data))
        return;

    vp8_surface = *data;

    if (vp8_surface->scaled_4x_surface_obj) {
        i965_DestroySurfaces(vp8_surface->ctx, &vp8_surface->scaled_4x_surface_id, 1);
        vp8_surface->scaled_4x_surface_id = VA_INVALID_SURFACE;
        vp8_surface->scaled_4x_surface_obj = NULL;
    }

    if (vp8_surface->scaled_16x_surface_obj) {
        i965_DestroySurfaces(vp8_surface->ctx, &vp8_surface->scaled_16x_surface_id, 1);
        vp8_surface->scaled_16x_surface_id = VA_INVALID_SURFACE;
        vp8_surface->scaled_16x_surface_obj = NULL;
    }
}

static void
i965_encoder_vp8_allocate_surfaces(VADriverContextP ctx,
                                   struct intel_encoder_context *encoder_context,
                                   struct object_surface *obj_surface,
                                   int forced_free)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_surface *vp8_surface;
    int down_scaled_width_4x, down_scaled_height_4x;
    int down_scaled_width_16x, down_scaled_height_16x;

    if (!obj_surface)
        return;

    if (obj_surface->private_data && obj_surface->free_private_data) {
        if (forced_free && obj_surface->free_private_data != i965_encoder_vp8_free_surfaces)
            obj_surface->free_private_data(obj_surface->private_data);
        else
            return;
    }

    vp8_surface = calloc(1, sizeof(struct i965_encoder_vp8_surface));

    if (!vp8_surface) {
        obj_surface->private_data = NULL;
        obj_surface->free_private_data = NULL;

        return;
    }

    vp8_surface->ctx = ctx;

    down_scaled_width_4x = vp8_context->down_scaled_width_4x;
    down_scaled_height_4x = vp8_context->down_scaled_height_4x;
    i965_CreateSurfaces(ctx,
                        down_scaled_width_4x,
                        down_scaled_height_4x,
                        VA_RT_FORMAT_YUV420,
                        1,
                        &vp8_surface->scaled_4x_surface_id);
    vp8_surface->scaled_4x_surface_obj = SURFACE(vp8_surface->scaled_4x_surface_id);

    if (vp8_surface->scaled_4x_surface_obj)
        i965_check_alloc_surface_bo(ctx, vp8_surface->scaled_4x_surface_obj, 1,
                                    VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);

    down_scaled_width_16x = vp8_context->down_scaled_width_16x;
    down_scaled_height_16x = vp8_context->down_scaled_height_16x;
    i965_CreateSurfaces(ctx,
                        down_scaled_width_16x,
                        down_scaled_height_16x,
                        VA_RT_FORMAT_YUV420,
                        1,
                        &vp8_surface->scaled_16x_surface_id);
    vp8_surface->scaled_16x_surface_obj = SURFACE(vp8_surface->scaled_16x_surface_id);

    if (vp8_surface->scaled_16x_surface_obj)
        i965_check_alloc_surface_bo(ctx, vp8_surface->scaled_16x_surface_obj, 1,
                                    VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);

    obj_surface->private_data = vp8_surface;
    obj_surface->free_private_data = i965_encoder_vp8_free_surfaces;
}

static void
i965_encoder_vp8_read_encode_status(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_encoder_vp8_encode_status_buffer *encode_status_buffer = &vp8_context->encode_status_buffer;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_mi_store_register_mem_parameter mi_store_register_mem_param;
    struct gpe_mi_flush_dw_parameter mi_flush_dw_param;
    unsigned int base_offset;

    base_offset = encode_status_buffer->base_offset;

    memset(&mi_flush_dw_param, 0, sizeof(mi_flush_dw_param));
    gpe->mi_flush_dw(ctx, batch, &mi_flush_dw_param);

    memset(&mi_store_register_mem_param, 0, sizeof(mi_store_register_mem_param));
    mi_store_register_mem_param.bo = encode_status_buffer->bo;
    mi_store_register_mem_param.offset = base_offset + encode_status_buffer->bitstream_byte_count_offset;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFC_BITSTREAM_BYTECOUNT_FRAME_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = base_offset + encode_status_buffer->image_status_mask_offset;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFC_IMAGE_STATUS_MASK_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = base_offset + encode_status_buffer->image_status_ctrl_offset;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFC_IMAGE_STATUS_CTRL_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    memset(&mi_flush_dw_param, 0, sizeof(mi_flush_dw_param));
    gpe->mi_flush_dw(ctx, batch, &mi_flush_dw_param);
}

static void
i965_encoder_vp8_read_pak_statistics(VADriverContextP ctx,
                                     struct intel_encoder_context *encoder_context,
                                     int ipass)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_mi_store_data_imm_parameter mi_store_data_imm_param;
    struct gpe_mi_store_register_mem_parameter mi_store_register_mem_param;
    struct gpe_mi_flush_dw_parameter mi_flush_dw_param;

    memset(&mi_flush_dw_param, 0, sizeof(mi_flush_dw_param));
    gpe->mi_flush_dw(ctx, batch, &mi_flush_dw_param);

    if (ipass < vp8_context->num_brc_pak_passes) {
        memset(&mi_store_data_imm_param, 0, sizeof(mi_store_data_imm_param));
        mi_store_data_imm_param.bo = vp8_context->brc_pak_statistics_buffer.bo;
        mi_store_data_imm_param.offset = sizeof(unsigned int) * 2;
        mi_store_data_imm_param.dw0 = (ipass + 1) << 8;
        gpe->mi_store_data_imm(ctx, batch, &mi_store_data_imm_param);
    }

    memset(&mi_store_register_mem_param, 0, sizeof(mi_store_register_mem_param));
    mi_store_register_mem_param.bo = vp8_context->brc_pak_statistics_buffer.bo;
    mi_store_register_mem_param.offset = 0;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFC_BITSTREAM_BYTECOUNT_FRAME_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 5;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_DQ_INDEX_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 6;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_D_LOOP_FILTER_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    if (ipass == 0) {
        mi_store_register_mem_param.offset = sizeof(unsigned int) * 4;
        mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_DQ_INDEX01_REG_OFFSET;
        gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);
    }

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 9;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_DQ_INDEX01_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 10;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_DQ_INDEX23_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 11;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_D_LOOP_FILTER01_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 12;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_D_LOOP_FILTER23_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

    mi_store_register_mem_param.offset = sizeof(unsigned int) * 13;
    mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CONVERGENCE_STATUS_REG_OFFSET;
    gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);
}

static void
i965_encoder_vp8_gpe_context_init_once(VADriverContextP ctx,
                                       struct i965_gpe_context *gpe_context,
                                       struct vp8_encoder_kernel_parameters *kernel_params,
                                       unsigned int idrt_entry_size)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);

    gpe_context->curbe.length = kernel_params->curbe_size; // in bytes

    gpe_context->sampler.entry_size = 0;
    gpe_context->sampler.max_entries = 0;

    gpe_context->idrt.entry_size = idrt_entry_size;
    gpe_context->idrt.max_entries = NUM_KERNELS_PER_GPE_CONTEXT;

    gpe_context->surface_state_binding_table.max_entries = MAX_VP8_ENCODER_SURFACES;
    gpe_context->surface_state_binding_table.binding_table_offset = 0;
    gpe_context->surface_state_binding_table.surface_state_offset = gpe_context->surface_state_binding_table.binding_table_offset +
                                                                    ALIGN(MAX_VP8_ENCODER_SURFACES * 4, 64);
    gpe_context->surface_state_binding_table.length = ALIGN(MAX_VP8_ENCODER_SURFACES * 4, 64) + ALIGN(MAX_VP8_ENCODER_SURFACES * SURFACE_STATE_PADDED_SIZE_GEN8, 64);

    if (i965->intel.eu_total > 0)
        gpe_context->vfe_state.max_num_threads = 6 * i965->intel.eu_total;
    else
        gpe_context->vfe_state.max_num_threads = 112;

    gpe_context->vfe_state.curbe_allocation_size = ALIGN(gpe_context->curbe.length, 32) >> 5; // in registers
    gpe_context->vfe_state.urb_entry_size = MAX(1, (ALIGN(kernel_params->inline_data_size, 32) +
                                                    ALIGN(kernel_params->external_data_size, 32)) >> 5); // in registers
    gpe_context->vfe_state.num_urb_entries = (MAX_URB_SIZE -
                                              gpe_context->vfe_state.curbe_allocation_size -
                                              ((gpe_context->idrt.entry_size >> 5) *
                                               gpe_context->idrt.max_entries)) / gpe_context->vfe_state.urb_entry_size;
    gpe_context->vfe_state.num_urb_entries = CLAMP(gpe_context->vfe_state.num_urb_entries, 1, 64);
    gpe_context->vfe_state.gpgpu_mode = 0;
}

static void
i965_encoder_vp8_gpe_context_vfe_scoreboard_init(struct i965_gpe_context *gpe_context, struct vp8_encoder_scoreboard_parameters *scoreboard_params)
{
    gpe_context->vfe_desc5.scoreboard0.mask = scoreboard_params->mask;
    gpe_context->vfe_desc5.scoreboard0.type = scoreboard_params->type;
    gpe_context->vfe_desc5.scoreboard0.enable = scoreboard_params->enable;

    // Scoreboard 0
    gpe_context->vfe_desc6.scoreboard1.delta_x0 = -1;
    gpe_context->vfe_desc6.scoreboard1.delta_y0 = 0;

    // Scoreboard 1
    gpe_context->vfe_desc6.scoreboard1.delta_x1 = 0;
    gpe_context->vfe_desc6.scoreboard1.delta_y1 = -1;

    // Scoreboard 2
    gpe_context->vfe_desc6.scoreboard1.delta_x2 = 1;
    gpe_context->vfe_desc6.scoreboard1.delta_y2 = -1;

    // Scoreboard 3
    gpe_context->vfe_desc6.scoreboard1.delta_x3 = -1;
    gpe_context->vfe_desc6.scoreboard1.delta_y3 = -1;

    // Scoreboard 4
    gpe_context->vfe_desc7.scoreboard2.delta_x4 = -1;
    gpe_context->vfe_desc7.scoreboard2.delta_y4 = 1;

    // Scoreboard 5
    gpe_context->vfe_desc7.scoreboard2.delta_x5 = 0;
    gpe_context->vfe_desc7.scoreboard2.delta_y5 = -2;
    // Scoreboard 6
    gpe_context->vfe_desc7.scoreboard2.delta_x6 = 1;
    gpe_context->vfe_desc7.scoreboard2.delta_y6 = -2;
    // Scoreboard 7
    gpe_context->vfe_desc7.scoreboard2.delta_x6 = -1;
    gpe_context->vfe_desc7.scoreboard2.delta_y6 = -2;
}

static void
i965_add_dri_buffer_gpe_surface(VADriverContextP ctx,
                                struct intel_encoder_context *encoder_context,
                                struct i965_gpe_context *gpe_context,
                                dri_bo *bo,
                                int is_raw_buffer,
                                unsigned int size,
                                unsigned int offset,
                                int index)
{
    struct i965_gpe_resource gpe_resource;

    i965_dri_object_to_buffer_gpe_resource(&gpe_resource, bo);
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &gpe_resource,
                                is_raw_buffer,
                                size,
                                offset,
                                index);

    i965_free_gpe_resource(&gpe_resource);
}

static void
i965_add_dri_buffer_2d_gpe_surface(VADriverContextP ctx,
                                   struct intel_encoder_context *encoder_context,
                                   struct i965_gpe_context *gpe_context,
                                   dri_bo *bo,
                                   unsigned int width,
                                   unsigned int height,
                                   unsigned int pitch,
                                   int is_media_block_rw,
                                   unsigned int format,
                                   int index)
{
    struct i965_gpe_resource gpe_resource;

    i965_dri_object_to_2d_gpe_resource(&gpe_resource, bo, width, height, pitch);
    i965_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &gpe_resource,
                                   is_media_block_rw,
                                   format,
                                   index);

    i965_free_gpe_resource(&gpe_resource);
}

static void
i965_run_kernel_media_object(VADriverContextP ctx,
                             struct intel_encoder_context *encoder_context,
                             struct i965_gpe_context *gpe_context,
                             int media_function,
                             struct gpe_media_object_parameter *param)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct intel_batchbuffer *batch = encoder_context->base.batch;

    intel_batchbuffer_start_atomic(batch, 0x1000);

    intel_batchbuffer_emit_mi_flush(batch);
    gpe->pipeline_setup(ctx, gpe_context, batch);
    gpe->media_object(ctx, gpe_context, batch, param);
    gpe->media_state_flush(ctx, gpe_context, batch);
    gpe->pipeline_end(ctx, gpe_context, batch);

    intel_batchbuffer_end_atomic(batch);

    intel_batchbuffer_flush(batch);
}

static void
i965_init_media_object_walker_parameters(struct intel_encoder_context *encoder_context,
                                         struct vp8_encoder_kernel_walker_parameter *kernel_walker_param,
                                         struct gpe_media_object_walker_parameter *walker_param)
{
    memset(walker_param, 0, sizeof(*walker_param));

    walker_param->use_scoreboard = kernel_walker_param->use_scoreboard;

    walker_param->block_resolution.x = kernel_walker_param->resolution_x;
    walker_param->block_resolution.y = kernel_walker_param->resolution_y;

    walker_param->global_resolution.x = kernel_walker_param->resolution_x;
    walker_param->global_resolution.y = kernel_walker_param->resolution_y;

    walker_param->global_outer_loop_stride.x = kernel_walker_param->resolution_x;
    walker_param->global_outer_loop_stride.y = 0;

    walker_param->global_inner_loop_unit.x = 0;
    walker_param->global_inner_loop_unit.y = kernel_walker_param->resolution_y;

    walker_param->local_loop_exec_count = 0xFFFF;  //MAX VALUE
    walker_param->global_loop_exec_count = 0xFFFF;  //MAX VALUE

    if (kernel_walker_param->no_dependency) {
        walker_param->scoreboard_mask = 0;

        // Raster scan walking pattern
        walker_param->local_outer_loop_stride.x = 0;
        walker_param->local_outer_loop_stride.y = 1;
        walker_param->local_inner_loop_unit.x = 1;
        walker_param->local_inner_loop_unit.y = 0;
        walker_param->local_end.x = kernel_walker_param->resolution_x - 1;
        walker_param->local_end.y = 0;
    } else {
        walker_param->local_end.x = 0;
        walker_param->local_end.y = 0;

        if (kernel_walker_param->walker_degree == VP8_ENCODER_46_DEGREE) {
            // 46 degree
            walker_param->scoreboard_mask = kernel_walker_param->scoreboard_mask;
            walker_param->local_outer_loop_stride.x = 1;
            walker_param->local_outer_loop_stride.y = 0;
            walker_param->local_inner_loop_unit.x = -1;
            walker_param->local_inner_loop_unit.y = 1;
        } else if (kernel_walker_param->walker_degree == VP8_ENCODER_45Z_DEGREE) {
            // 45z degree
            walker_param->scoreboard_mask = 0x0F;

            walker_param->global_loop_exec_count = 0x3FF;
            walker_param->local_loop_exec_count = 0x3FF;

            walker_param->global_resolution.x = (unsigned int)(kernel_walker_param->resolution_x / 2.f) + 1;
            walker_param->global_resolution.y = 2 * kernel_walker_param->resolution_y;

            walker_param->global_start.x = 0;
            walker_param->global_start.y = 0;

            walker_param->global_outer_loop_stride.x = walker_param->global_resolution.x;
            walker_param->global_outer_loop_stride.y = 0;

            walker_param->global_inner_loop_unit.x = 0;
            walker_param->global_inner_loop_unit.y = walker_param->global_resolution.y;

            walker_param->block_resolution.x = walker_param->global_resolution.x;
            walker_param->block_resolution.y = walker_param->global_resolution.y;

            walker_param->local_start.x = 0;
            walker_param->local_start.y = 0;

            walker_param->local_outer_loop_stride.x = 1;
            walker_param->local_outer_loop_stride.y = 0;

            walker_param->local_inner_loop_unit.x = -1;
            walker_param->local_inner_loop_unit.y = 4;

            walker_param->middle_loop_extra_steps = 3;
            walker_param->mid_loop_unit_x = 0;
            walker_param->mid_loop_unit_y = 1;
        } else if (kernel_walker_param->walker_degree == VP8_ENCODER_45_DEGREE) {
            // 45 degree
            walker_param->scoreboard_mask = 0x03;
            walker_param->local_outer_loop_stride.x = 1;
            walker_param->local_outer_loop_stride.y = 0;
            walker_param->local_inner_loop_unit.x = -1;
            walker_param->local_inner_loop_unit.y = 1;
        } else {
            // 26 degree
            walker_param->scoreboard_mask = 0x0F;
            walker_param->local_outer_loop_stride.x = 1;
            walker_param->local_outer_loop_stride.y = 0;
            walker_param->local_inner_loop_unit.x = -2;
            walker_param->local_inner_loop_unit.y = 1;
        }
    }
}

static void
i965_run_kernel_media_object_walker(VADriverContextP ctx,
                                    struct intel_encoder_context *encoder_context,
                                    struct i965_gpe_context *gpe_context,
                                    int media_function,
                                    struct gpe_media_object_walker_parameter *param)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct intel_batchbuffer *batch = encoder_context->base.batch;

    intel_batchbuffer_start_atomic(batch, 0x1000);

    intel_batchbuffer_emit_mi_flush(batch);
    gpe->pipeline_setup(ctx, gpe_context, batch);
    gpe->media_object_walker(ctx, gpe_context, batch, param);
    gpe->media_state_flush(ctx, gpe_context, batch);
    gpe->pipeline_end(ctx, gpe_context, batch);

    intel_batchbuffer_end_atomic(batch);

    intel_batchbuffer_flush(batch);
}

static void
i965_encoder_vp8_vme_init_mpu_tpu_buffer(VADriverContextP ctx,
                                         struct intel_encoder_context *encoder_context,
                                         struct i965_encoder_vp8_context *vp8_context)
{
    char *pbuffer = NULL;

    i965_zero_gpe_resource(&vp8_context->pak_mpu_tpu_mode_probs_buffer);
    i965_zero_gpe_resource(&vp8_context->pak_mpu_tpu_ref_mode_probs_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_default_coef_probs, sizeof(vp8_default_coef_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_entropy_cost_table_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_prob_cost, sizeof(vp8_prob_cost));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_entropy_cost_table_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_pak_token_update_flags_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_probs_update_flag, sizeof(vp8_probs_update_flag));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_pak_token_update_flags_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_default_token_probability_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_coef_update_probs, sizeof(vp8_coef_update_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_default_token_probability_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_default_coef_probs, sizeof(vp8_default_coef_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_updated_token_probability_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_default_coef_probs, sizeof(vp8_default_coef_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_updated_token_probability_buffer);
}

#define ALLOC_VP8_RESOURCE_BUFFER(buffer, bufsize, des)         \
    do {                                                        \
        vp8_context->buffer.type = I965_GPE_RESOURCE_BUFFER;    \
        vp8_context->buffer.width = (bufsize);                  \
        vp8_context->buffer.height = 1;                         \
        vp8_context->buffer.pitch = vp8_context->buffer.width;  \
        vp8_context->buffer.size = vp8_context->buffer.pitch *  \
            vp8_context->buffer.height;                         \
        vp8_context->buffer.tiling = I915_TILING_NONE;          \
        i965_allocate_gpe_resource(i965->intel.bufmgr,          \
                                   &vp8_context->buffer,        \
                                   vp8_context->buffer.size,    \
                                   (des));                      \
    } while (0)

static void
i965_encoder_vp8_vme_allocate_resources(VADriverContextP ctx,
                                        struct intel_encoder_context *encoder_context,
                                        struct i965_encoder_vp8_context *vp8_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    unsigned int frame_size_in_mbs = vp8_context->frame_width_in_mbs *
                                     vp8_context->frame_height_in_mbs;

    vp8_context->mv_offset = ALIGN((frame_size_in_mbs * 16 * 4), 4096);
    vp8_context->mb_coded_buffer_size = vp8_context->mv_offset + (frame_size_in_mbs * 16 * sizeof(unsigned int));

    ALLOC_VP8_RESOURCE_BUFFER(reference_frame_mb_count_buffer, 32, "Reference frame mb count buffer");

    vp8_context->mb_mode_cost_luma_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->mb_mode_cost_luma_buffer.width = ALIGN((sizeof(short) * 10), 64);
    vp8_context->mb_mode_cost_luma_buffer.height = 1;
    vp8_context->mb_mode_cost_luma_buffer.pitch = vp8_context->mb_mode_cost_luma_buffer.width;
    vp8_context->mb_mode_cost_luma_buffer.size = vp8_context->mb_mode_cost_luma_buffer.pitch *
                                                 vp8_context->mb_mode_cost_luma_buffer.height;
    vp8_context->mb_mode_cost_luma_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->mb_mode_cost_luma_buffer,
                               vp8_context->mb_mode_cost_luma_buffer.size,
                               "MB mode cost luma buffer");

    vp8_context->block_mode_cost_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->block_mode_cost_buffer.width = ALIGN((sizeof(short) * 10 * 10 * 10), 64);
    vp8_context->block_mode_cost_buffer.height = 1;
    vp8_context->block_mode_cost_buffer.pitch = vp8_context->block_mode_cost_buffer.width;
    vp8_context->block_mode_cost_buffer.size = vp8_context->block_mode_cost_buffer.pitch *
                                               vp8_context->block_mode_cost_buffer.height;
    vp8_context->block_mode_cost_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->block_mode_cost_buffer,
                               vp8_context->block_mode_cost_buffer.size,
                               "Block mode cost luma buffer");

    ALLOC_VP8_RESOURCE_BUFFER(chroma_recon_buffer, frame_size_in_mbs * 64, "Chroma recon buffer");

    vp8_context->per_mb_quant_data_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->per_mb_quant_data_buffer.width = ALIGN((vp8_context->frame_width_in_mbs * 4), 64);
    vp8_context->per_mb_quant_data_buffer.height = vp8_context->frame_height_in_mbs;
    vp8_context->per_mb_quant_data_buffer.pitch = vp8_context->per_mb_quant_data_buffer.width;
    vp8_context->per_mb_quant_data_buffer.size = vp8_context->per_mb_quant_data_buffer.pitch *
                                                 vp8_context->per_mb_quant_data_buffer.height;
    vp8_context->per_mb_quant_data_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->per_mb_quant_data_buffer,
                               vp8_context->per_mb_quant_data_buffer.size,
                               "Per MB quant data buffer");

    ALLOC_VP8_RESOURCE_BUFFER(pred_mv_data_buffer, frame_size_in_mbs * 4 * sizeof(unsigned int), "Pred mv data buffer");
    ALLOC_VP8_RESOURCE_BUFFER(mode_cost_update_buffer, 16 * sizeof(unsigned int), "Mode cost update buffer");

    /*
     * BRC buffers
     */
    ALLOC_VP8_RESOURCE_BUFFER(brc_history_buffer, VP8_BRC_HISTORY_BUFFER_SIZE, "BRC history buffer");
    i965_zero_gpe_resource(&vp8_context->brc_history_buffer);

    vp8_context->brc_segment_map_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->brc_segment_map_buffer.width = vp8_context->frame_width_in_mbs;
    vp8_context->brc_segment_map_buffer.height = vp8_context->frame_height_in_mbs;
    vp8_context->brc_segment_map_buffer.pitch = vp8_context->brc_segment_map_buffer.width;
    vp8_context->brc_segment_map_buffer.size = vp8_context->brc_segment_map_buffer.pitch *
                                               vp8_context->brc_segment_map_buffer.height;
    vp8_context->brc_segment_map_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->brc_segment_map_buffer,
                               vp8_context->brc_segment_map_buffer.size,
                               "BRC segment map buffer");

    vp8_context->brc_distortion_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->brc_distortion_buffer.width = ALIGN((vp8_context->down_scaled_width_in_mb4x * 8), 64);
    vp8_context->brc_distortion_buffer.height = 2 * ALIGN((vp8_context->down_scaled_height_in_mb4x * 4), 8);
    vp8_context->brc_distortion_buffer.pitch = vp8_context->brc_distortion_buffer.width;
    vp8_context->brc_distortion_buffer.size = vp8_context->brc_distortion_buffer.pitch *
                                              vp8_context->brc_distortion_buffer.height;
    vp8_context->brc_distortion_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->brc_distortion_buffer,
                               vp8_context->brc_distortion_buffer.size,
                               "BRC distortion buffer");
    i965_zero_gpe_resource(&vp8_context->brc_distortion_buffer);

    ALLOC_VP8_RESOURCE_BUFFER(brc_pak_statistics_buffer, sizeof(struct vp8_brc_pak_statistics), "BRC pak statistics buffer");
    i965_zero_gpe_resource(&vp8_context->brc_pak_statistics_buffer);

    ALLOC_VP8_RESOURCE_BUFFER(brc_vp8_cfg_command_read_buffer, VP8_BRC_IMG_STATE_SIZE_PER_PASS * VP8_BRC_MAXIMUM_NUM_PASSES, "BRC VP8 configuration command read buffer");
    i965_zero_gpe_resource(&vp8_context->brc_vp8_cfg_command_read_buffer);

    ALLOC_VP8_RESOURCE_BUFFER(brc_vp8_cfg_command_write_buffer, VP8_BRC_IMG_STATE_SIZE_PER_PASS * VP8_BRC_MAXIMUM_NUM_PASSES, "BRC VP8 configuration command write buffer");
    i965_zero_gpe_resource(&vp8_context->brc_vp8_cfg_command_write_buffer);

    ALLOC_VP8_RESOURCE_BUFFER(brc_vp8_constant_data_buffer, VP8_BRC_CONSTANT_DATA_SIZE, "BRC VP8 constant data buffer");
    i965_zero_gpe_resource(&vp8_context->brc_vp8_constant_data_buffer);

    ALLOC_VP8_RESOURCE_BUFFER(brc_pak_statistics_dump_buffer, vp8_context->num_brc_pak_passes * sizeof(unsigned int) * 12, "BRC pak statistics buffer");
    i965_zero_gpe_resource(&vp8_context->brc_pak_statistics_dump_buffer);

    vp8_context->me_4x_mv_data_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->me_4x_mv_data_buffer.width = vp8_context->down_scaled_width_in_mb4x * 32;
    vp8_context->me_4x_mv_data_buffer.height = vp8_context->down_scaled_height_in_mb4x * 4 * 4;
    vp8_context->me_4x_mv_data_buffer.pitch = ALIGN(vp8_context->me_4x_mv_data_buffer.width, 64);
    vp8_context->me_4x_mv_data_buffer.size = vp8_context->me_4x_mv_data_buffer.pitch *
                                             vp8_context->me_4x_mv_data_buffer.height;
    vp8_context->me_4x_mv_data_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->me_4x_mv_data_buffer,
                               vp8_context->me_4x_mv_data_buffer.size,
                               "ME 4x MV Data buffer");

    vp8_context->me_4x_distortion_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->me_4x_distortion_buffer.width = vp8_context->down_scaled_width_in_mb4x * 8;
    vp8_context->me_4x_distortion_buffer.height = vp8_context->down_scaled_height_in_mb4x * 4 * 4;
    vp8_context->me_4x_distortion_buffer.pitch = ALIGN(vp8_context->me_4x_distortion_buffer.width, 64);
    vp8_context->me_4x_distortion_buffer.size = vp8_context->me_4x_distortion_buffer.pitch *
                                                vp8_context->me_4x_distortion_buffer.height;
    vp8_context->me_4x_distortion_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->me_4x_distortion_buffer,
                               vp8_context->me_4x_distortion_buffer.size,
                               "ME 4x Distortion buffer");

    vp8_context->me_16x_mv_data_buffer.type = I965_GPE_RESOURCE_2D;
    vp8_context->me_16x_mv_data_buffer.width = ALIGN((vp8_context->down_scaled_width_in_mb16x * 32), 64);
    vp8_context->me_16x_mv_data_buffer.height = vp8_context->down_scaled_height_in_mb16x * 4 * VP8_ME_MV_DATA_SIZE_MULTIPLIER;
    vp8_context->me_16x_mv_data_buffer.pitch = vp8_context->me_16x_mv_data_buffer.width;
    vp8_context->me_16x_mv_data_buffer.size = vp8_context->me_16x_mv_data_buffer.pitch *
                                              vp8_context->me_16x_mv_data_buffer.height;
    vp8_context->me_16x_mv_data_buffer.tiling = I915_TILING_NONE;
    i965_allocate_gpe_resource(i965->intel.bufmgr,
                               &vp8_context->me_16x_mv_data_buffer,
                               vp8_context->me_16x_mv_data_buffer.size,
                               "ME 16x MV Data buffer");

    ALLOC_VP8_RESOURCE_BUFFER(histogram_buffer, VP8_HISTOGRAM_SIZE, "Histogram buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_intra_row_store_scratch_buffer, vp8_context->frame_width_in_mbs * 64, "Intra row store scratch buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_deblocking_filter_row_store_scratch_buffer, vp8_context->frame_width_in_mbs * 4 * 64, "Deblocking filter row store scratch buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpc_row_store_scratch_buffer, vp8_context->frame_width_in_mbs * 2 * 64, "MPC row store scratch buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_stream_out_buffer, frame_size_in_mbs * 16, "stream out buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_frame_header_buffer, VP8_FRAME_HEADER_SIZE, "Frame header buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_intermediate_buffer, frame_size_in_mbs * 256 * 2 + frame_size_in_mbs * 64 + VP8_INTERMEDIATE_PARTITION0_SIZE, "Intermediate buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_mode_probs_buffer, VP8_MODE_PROPABILITIES_SIZE, "Mode probs buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_ref_mode_probs_buffer, VP8_MODE_PROPABILITIES_SIZE, "Ref mode probs buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_coeff_probs_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Coeff probs buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_ref_coeff_probs_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Ref coeff probs buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_token_bits_data_buffer, VP8_TOKEN_BITS_DATA_SIZE, "Token bits data buffer");
    i965_zero_gpe_resource(&vp8_context->pak_mpu_tpu_token_bits_data_buffer);
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_picture_state_buffer, VP8_PICTURE_STATE_SIZE, "Picture state buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_mpu_bitstream_buffer, VP8_MPU_BITSTREAM_SIZE, "Mpu bitstream buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_tpu_bitstream_buffer, VP8_TPU_BITSTREAM_SIZE, "Tpu bitstream buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_entropy_cost_table_buffer, VP8_ENTROPY_COST_TABLE_SIZE, "Entropy cost buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_pak_token_statistics_buffer, VP8_TOKEN_STATISTICS_SIZE, "Pak token statistics buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_pak_token_update_flags_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Pak token update flags buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_default_token_probability_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Default token probability buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_key_frame_token_probability_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Key frame token probability buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_updated_token_probability_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Updated token probability buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_hw_token_probability_pak_pass_2_buffer, VP8_COEFFS_PROPABILITIES_SIZE, "Hw token probability pak pass 2 buffer");
    ALLOC_VP8_RESOURCE_BUFFER(pak_mpu_tpu_repak_decision_buffer, VP8_REPAK_DECISION_BUF_SIZE, "Tpu repak decision buffer");

    i965_encoder_vp8_vme_init_mpu_tpu_buffer(ctx, encoder_context, vp8_context);

    ALLOC_VP8_RESOURCE_BUFFER(mb_coded_buffer, vp8_context->mb_coded_buffer_size, "MB coded buffer");
}

#undef ALLOC_VP8_RESOURCE_BUFFER

static void
i965_encoder_vp8_vme_free_resources(struct i965_encoder_vp8_context *vp8_context)
{
    i965_free_gpe_resource(&vp8_context->reference_frame_mb_count_buffer);
    i965_free_gpe_resource(&vp8_context->mb_mode_cost_luma_buffer);
    i965_free_gpe_resource(&vp8_context->block_mode_cost_buffer);
    i965_free_gpe_resource(&vp8_context->chroma_recon_buffer);
    i965_free_gpe_resource(&vp8_context->per_mb_quant_data_buffer);
    i965_free_gpe_resource(&vp8_context->pred_mv_data_buffer);
    i965_free_gpe_resource(&vp8_context->mode_cost_update_buffer);

    i965_free_gpe_resource(&vp8_context->brc_history_buffer);
    i965_free_gpe_resource(&vp8_context->brc_segment_map_buffer);
    i965_free_gpe_resource(&vp8_context->brc_distortion_buffer);
    i965_free_gpe_resource(&vp8_context->brc_pak_statistics_buffer);
    i965_free_gpe_resource(&vp8_context->brc_vp8_cfg_command_read_buffer);
    i965_free_gpe_resource(&vp8_context->brc_vp8_cfg_command_write_buffer);
    i965_free_gpe_resource(&vp8_context->brc_vp8_constant_data_buffer);
    i965_free_gpe_resource(&vp8_context->brc_pak_statistics_dump_buffer);

    i965_free_gpe_resource(&vp8_context->me_4x_mv_data_buffer);
    i965_free_gpe_resource(&vp8_context->me_4x_distortion_buffer);
    i965_free_gpe_resource(&vp8_context->me_16x_mv_data_buffer);

    i965_free_gpe_resource(&vp8_context->histogram_buffer);

    i965_free_gpe_resource(&vp8_context->pak_intra_row_store_scratch_buffer);
    i965_free_gpe_resource(&vp8_context->pak_deblocking_filter_row_store_scratch_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpc_row_store_scratch_buffer);
    i965_free_gpe_resource(&vp8_context->pak_stream_out_buffer);
    i965_free_gpe_resource(&vp8_context->pak_frame_header_buffer);
    i965_free_gpe_resource(&vp8_context->pak_intermediate_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_mode_probs_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_ref_mode_probs_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_coeff_probs_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_token_bits_data_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_mpu_bitstream_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_tpu_bitstream_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_entropy_cost_table_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_pak_token_statistics_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_pak_token_update_flags_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_default_token_probability_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_updated_token_probability_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_hw_token_probability_pak_pass_2_buffer);
    i965_free_gpe_resource(&vp8_context->pak_mpu_tpu_repak_decision_buffer);

    i965_free_gpe_resource(&vp8_context->mb_coded_buffer);
}

static void
i965_encoder_vp8_update_internal_rc_mode(VADriverContextP ctx,
                                         struct encode_state *encode_state,
                                         struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    if (encoder_context->rate_control_mode & VA_RC_CBR)
        vp8_context->internal_rate_mode = I965_BRC_CBR;
    else if (encoder_context->rate_control_mode & VA_RC_VBR)
        vp8_context->internal_rate_mode = I965_BRC_VBR;
    else
        vp8_context->internal_rate_mode = I965_BRC_CQP;
}

static void
i965_encoder_vp8_get_sequence_parameter(VADriverContextP ctx,
                                        struct encode_state *encode_state,
                                        struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    /*
     * It is required to update frame width and height for each frame
     */
    if (encoder_context->frame_width_in_pixel != vp8_context->picture_width ||
        encoder_context->frame_height_in_pixel != vp8_context->picture_height) {
        vp8_context->picture_width = encoder_context->frame_width_in_pixel;
        vp8_context->picture_height = encoder_context->frame_height_in_pixel;

        vp8_context->frame_width_in_mbs = WIDTH_IN_MACROBLOCKS(vp8_context->picture_width);
        vp8_context->frame_height_in_mbs = HEIGHT_IN_MACROBLOCKS(vp8_context->picture_height);

        vp8_context->frame_width = vp8_context->frame_width_in_mbs * 16;
        vp8_context->frame_height = vp8_context->frame_height_in_mbs * 16;

        vp8_context->down_scaled_width_in_mb4x = WIDTH_IN_MACROBLOCKS(vp8_context->frame_width / SCALE_FACTOR_4X);
        vp8_context->down_scaled_height_in_mb4x = HEIGHT_IN_MACROBLOCKS(vp8_context->frame_height / SCALE_FACTOR_4X);
        vp8_context->down_scaled_width_4x = vp8_context->down_scaled_width_in_mb4x * 16;
        vp8_context->down_scaled_height_4x = vp8_context->down_scaled_height_in_mb4x * 16;

        vp8_context->down_scaled_width_in_mb16x = WIDTH_IN_MACROBLOCKS(vp8_context->frame_width / SCALE_FACTOR_16X);
        vp8_context->down_scaled_height_in_mb16x = HEIGHT_IN_MACROBLOCKS(vp8_context->frame_height / SCALE_FACTOR_16X);
        vp8_context->down_scaled_width_16x = vp8_context->down_scaled_width_in_mb16x * 16;
        vp8_context->down_scaled_height_16x = vp8_context->down_scaled_height_in_mb16x * 16;

        i965_encoder_vp8_check_motion_estimation(ctx, encoder_context);

        i965_encoder_vp8_vme_free_resources(vp8_context);
        i965_encoder_vp8_vme_allocate_resources(ctx, encoder_context, vp8_context);
    }

    vp8_context->num_passes = 0;
    vp8_context->repak_pass_iter_val = 0;
    vp8_context->ref_ctrl_optimization_done = 0;
}

static void
i965_encoder_vp8_get_picture_parameter(VADriverContextP ctx,
                                       struct encode_state *encode_state,
                                       struct intel_encoder_context *encoder_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct object_surface *obj_surface;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_params = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    int is_intra, i;
    unsigned int average_qp = 0;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    vp8_context->frame_type = pic_param->pic_flags.bits.frame_type ? MPEG_P_PICTURE : MPEG_I_PICTURE;
    is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);

    if (is_intra) {
        vp8_context->ref_frame_ctrl = 0;
    } else {
        vp8_context->ref_frame_ctrl =
            ((!pic_param->ref_flags.bits.no_ref_last) |
             (!pic_param->ref_flags.bits.no_ref_gf << 1) |
             (!pic_param->ref_flags.bits.no_ref_arf << 2));
    }

    vp8_context->hme_enabled = (vp8_context->hme_supported && !is_intra && vp8_context->ref_frame_ctrl != 0);
    vp8_context->hme_16x_enabled = (vp8_context->hme_16x_supported && !is_intra);

    if (pic_param->ref_last_frame != VA_INVALID_SURFACE) {
        obj_surface = SURFACE(pic_param->ref_last_frame);

        if (obj_surface && obj_surface->bo)
            vp8_context->ref_last_frame = obj_surface;
        else
            vp8_context->ref_last_frame = NULL;
    } else {
        vp8_context->ref_last_frame = NULL;
    }

    if (pic_param->ref_gf_frame != VA_INVALID_SURFACE) {
        obj_surface = SURFACE(pic_param->ref_gf_frame);

        if (obj_surface && obj_surface->bo)
            vp8_context->ref_gf_frame = obj_surface;
        else
            vp8_context->ref_gf_frame = NULL;
    } else {
        vp8_context->ref_gf_frame = NULL;
    }

    if (pic_param->ref_arf_frame != VA_INVALID_SURFACE) {
        obj_surface = SURFACE(pic_param->ref_arf_frame);

        if (obj_surface && obj_surface->bo)
            vp8_context->ref_arf_frame = obj_surface;
        else
            vp8_context->ref_arf_frame = NULL;
    } else {
        vp8_context->ref_arf_frame = NULL;
    }

    vp8_context->brc_distortion_buffer_need_reset = 0;

    if (brc_enabled) {
        if (is_intra) {
            vp8_context->brc_distortion_buffer_need_reset = 1;
        } else {
            if (vp8_context->frame_num % vp8_context->gop_size == 1) {
                vp8_context->brc_distortion_buffer_need_reset = 1;
            }
        }
    }

    if (pic_param->pic_flags.bits.segmentation_enabled) {
        for (i = 0; i < VP8_MAX_SEGMENTS; i++) {
            average_qp += quant_params->quantization_index[i] + quant_params->quantization_index_delta[i];
        }

        average_qp = average_qp / VP8_MAX_SEGMENTS;
    } else {
        average_qp += quant_params->quantization_index[0] + quant_params->quantization_index_delta[0];
    }

    if (is_intra) {
        vp8_context->average_i_frame_qp = average_qp;
    } else {
        vp8_context->average_p_frame_qp = average_qp;
    }

    if (brc_enabled && vp8_context->multiple_pass_brc_supported)
        vp8_context->num_brc_pak_passes = VP8_BRC_MINIMUM_NUM_PASSES;
    else
        vp8_context->num_brc_pak_passes = VP8_BRC_SINGLE_PASS;

    vp8_context->num_passes = 0;
    vp8_context->min_pak_passes = 1;
    vp8_context->repak_pass_iter_val = 0;

    if (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) {
        vp8_context->num_passes = 1;
        vp8_context->min_pak_passes = 2;
    } else if (encoder_context->quality_level == ENCODER_LOW_QUALITY) {
        vp8_context->num_passes = 0;
        vp8_context->min_pak_passes = 1;
    } else {
        vp8_context->num_passes = 0;
        vp8_context->min_pak_passes = 1;
    }

    if (!vp8_context->repak_supported) {
        vp8_context->num_passes = 0;
        vp8_context->min_pak_passes = 1;
    }

    if (brc_enabled)
        vp8_context->num_passes += (vp8_context->num_brc_pak_passes - 1);

    if (vp8_context->repak_supported && vp8_context->min_pak_passes > 1)
        vp8_context->repak_pass_iter_val = vp8_context->num_passes;
}

static void
i965_encoder_vp8_get_misc_parameters(VADriverContextP ctx,
                                     struct encode_state *encode_state,
                                     struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    if (vp8_context->internal_rate_mode == I965_BRC_CQP) {
        vp8_context->init_vbv_buffer_fullness_in_bit = 0;
        vp8_context->vbv_buffer_size_in_bit = 0;
        vp8_context->target_bit_rate = 0;
        vp8_context->max_bit_rate = 0;
        vp8_context->min_bit_rate = 0;
        vp8_context->brc_need_reset = 0;
    } else {
        vp8_context->gop_size = encoder_context->brc.gop_size;

        if (encoder_context->brc.need_reset) {
            vp8_context->framerate = encoder_context->brc.framerate[0];
            vp8_context->vbv_buffer_size_in_bit = encoder_context->brc.hrd_buffer_size;
            vp8_context->init_vbv_buffer_fullness_in_bit = encoder_context->brc.hrd_initial_buffer_fullness;
            vp8_context->max_bit_rate = encoder_context->brc.bits_per_second[0]; // currently only one layer is supported
            vp8_context->brc_need_reset = (vp8_context->brc_initted && encoder_context->brc.need_reset);

            if (vp8_context->internal_rate_mode == I965_BRC_CBR) {
                vp8_context->min_bit_rate = vp8_context->max_bit_rate;
                vp8_context->target_bit_rate = vp8_context->max_bit_rate;
            } else {
                assert(vp8_context->internal_rate_mode == I965_BRC_VBR);

                if (encoder_context->brc.target_percentage[0] <= 50)
                    vp8_context->min_bit_rate = 0;
                else
                    vp8_context->min_bit_rate = vp8_context->max_bit_rate * (2 * encoder_context->brc.target_percentage[0] - 100) / 100;

                vp8_context->target_bit_rate = vp8_context->max_bit_rate * encoder_context->brc.target_percentage[0] / 100;
            }
        }
    }

    if (encoder_context->quality_level == ENCODER_LOW_QUALITY)
        vp8_context->hme_16x_supported = 0;
}

static VAStatus
i965_encoder_vp8_get_paramters(VADriverContextP ctx,
                               struct encode_state *encode_state,
                               struct intel_encoder_context *encoder_context)
{
    VAQMatrixBufferVP8 *quant_params = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    struct i965_encoder_vp8_surface *vp8_surface;

    i965_encoder_vp8_update_internal_rc_mode(ctx, encode_state, encoder_context);
    i965_encoder_vp8_get_sequence_parameter(ctx, encode_state, encoder_context);
    i965_encoder_vp8_get_misc_parameters(ctx, encode_state, encoder_context);
    i965_encoder_vp8_get_picture_parameter(ctx, encode_state, encoder_context);

    i965_encoder_vp8_allocate_surfaces(ctx, encoder_context, encode_state->reconstructed_object, 1);
    vp8_surface = encode_state->reconstructed_object->private_data;
    vp8_surface->qp_index = quant_params->quantization_index[0];

    return VA_STATUS_SUCCESS;
}

static VAStatus
i965_encoder_vp8_vme_gpe_kernel_init(VADriverContextP ctx,
                                     struct encode_state *encode_state,
                                     struct intel_encoder_context *encoder_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_encoder_vp8_mbenc_context *mbenc_context = &vp8_context->mbenc_context;
    struct i965_encoder_vp8_mpu_context *mpu_context = &vp8_context->mpu_context;
    struct i965_encoder_vp8_tpu_context *tpu_context = &vp8_context->tpu_context;
    struct gpe_dynamic_state_parameter ds_param;
    int i;

    /*
     * BRC will update MBEnc curbe data buffer, so initialize GPE context for
     * MBEnc first
     */
    for (i = 0; i < NUM_VP8_MBENC; i++) {
        gpe->context_init(ctx, &mbenc_context->gpe_contexts[i]);
    }

    /*
     * VP8_MBENC_I_FRAME_LUMA and VP8_MBENC_I_FRAME_CHROMA will use the same
     * the dynamic state buffer,
     */
    ds_param.bo_size = ALIGN(MAX(sizeof(struct vp8_mbenc_i_frame_curbe_data), sizeof(struct vp8_mbenc_p_frame_curbe_data)), 64) +
                       vp8_context->idrt_entry_size * 2;
    mbenc_context->luma_chroma_dynamic_buffer = dri_bo_alloc(i965->intel.bufmgr,
                                                             "IFrame Luma & CHROMA curbe buffer",
                                                             ds_param.bo_size,
                                                             0x1000);

    /*
     * VP8_MBENC_I_FRAME_LUMA and VP8_MBENC_I_FRAME_CHROMA will share the same
     * the curbe data buffer
     */
    ds_param.bo = mbenc_context->luma_chroma_dynamic_buffer;
    ds_param.curbe_offset = 0;
    ds_param.idrt_offset = ALIGN(MAX(sizeof(struct vp8_mbenc_i_frame_curbe_data), sizeof(struct vp8_mbenc_p_frame_curbe_data)), 64);
    ds_param.sampler_offset = ds_param.bo_size;
    gpe->set_dynamic_buffer(ctx, &mbenc_context->gpe_contexts[VP8_MBENC_I_FRAME_LUMA], &ds_param);

    ds_param.idrt_offset = ds_param.idrt_offset + vp8_context->idrt_entry_size;
    gpe->set_dynamic_buffer(ctx, &mbenc_context->gpe_contexts[VP8_MBENC_I_FRAME_CHROMA], &ds_param);

    /*
     * BRC will update MPU curbe data buffer, so initialize GPE context for
     * MPU first
     */
    gpe->context_init(ctx, &mpu_context->gpe_contexts[0]);
    ds_param.bo_size = ALIGN(sizeof(struct vp8_mpu_curbe_data), 64) + vp8_context->idrt_entry_size;
    mpu_context->dynamic_buffer = dri_bo_alloc(i965->intel.bufmgr,
                                               "MPU dynamic buffer",
                                               ds_param.bo_size,
                                               0x1000);

    ds_param.bo = mpu_context->dynamic_buffer;
    ds_param.curbe_offset = 0;
    ds_param.idrt_offset = ALIGN(sizeof(struct vp8_mpu_curbe_data), 64);
    ds_param.sampler_offset = ds_param.bo_size;
    gpe->set_dynamic_buffer(ctx, &mpu_context->gpe_contexts[0], &ds_param);

    /*
     * BRC will update TPU curbe data buffer, so initialize GPE context for
     * TPU first
     */
    gpe->context_init(ctx, &tpu_context->gpe_contexts[0]);
    ds_param.bo_size = ALIGN(sizeof(struct vp8_tpu_curbe_data), 64) + vp8_context->idrt_entry_size;
    tpu_context->dynamic_buffer = dri_bo_alloc(i965->intel.bufmgr,
                                               "MPU dynamic buffer",
                                               ds_param.bo_size,
                                               0x1000);

    ds_param.bo = tpu_context->dynamic_buffer;
    ds_param.curbe_offset = 0;
    ds_param.idrt_offset = ALIGN(sizeof(struct vp8_tpu_curbe_data), 64);
    ds_param.sampler_offset = ds_param.bo_size;
    gpe->set_dynamic_buffer(ctx, &tpu_context->gpe_contexts[0], &ds_param);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_brc_init_reset_set_curbe(VADriverContextP ctx,
                                              struct encode_state *encode_state,
                                              struct intel_encoder_context *encoder_context,
                                              struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    struct vp8_brc_init_reset_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    double input_bits_per_frame, bps_ratio;

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw0.profile_level_max_frame = vp8_context->frame_width * vp8_context->frame_height;
    pcmd->dw1.init_buf_full_in_bits = vp8_context->init_vbv_buffer_fullness_in_bit;
    pcmd->dw2.buf_size_in_bits = vp8_context->vbv_buffer_size_in_bit;
    pcmd->dw3.average_bitrate = (vp8_context->target_bit_rate + VP8_BRC_KBPS - 1) / VP8_BRC_KBPS * VP8_BRC_KBPS;
    pcmd->dw4.max_bitrate = (vp8_context->max_bit_rate + VP8_BRC_KBPS - 1) / VP8_BRC_KBPS * VP8_BRC_KBPS;
    pcmd->dw6.frame_rate_m = vp8_context->framerate.num;
    pcmd->dw7.frame_rate_d = vp8_context->framerate.den;
    pcmd->dw8.brc_flag = 0;
    pcmd->dw8.gop_minus1 = vp8_context->gop_size - 1;

    if (vp8_context->internal_rate_mode == I965_BRC_CBR) {
        pcmd->dw4.max_bitrate = pcmd->dw3.average_bitrate;

        pcmd->dw8.brc_flag = pcmd->dw8.brc_flag | BRC_KERNEL_CBR;
    } else if (vp8_context->internal_rate_mode == I965_BRC_VBR) {
        if (pcmd->dw4.max_bitrate < pcmd->dw3.average_bitrate) {
            pcmd->dw4.max_bitrate = 2 * pcmd->dw3.average_bitrate;
        }

        pcmd->dw8.brc_flag = pcmd->dw8.brc_flag | BRC_KERNEL_VBR;
    }

    input_bits_per_frame =
        ((double)(pcmd->dw4.max_bitrate) * (double)(pcmd->dw7.frame_rate_d) /
         (double)(pcmd->dw6.frame_rate_m));

    if (pcmd->dw2.buf_size_in_bits < (unsigned int)input_bits_per_frame * 4) {
        pcmd->dw2.buf_size_in_bits = (unsigned int)input_bits_per_frame * 4;
    }

    if (pcmd->dw1.init_buf_full_in_bits == 0) {
        pcmd->dw1.init_buf_full_in_bits = 7 * pcmd->dw2.buf_size_in_bits / 8;
    }

    if (pcmd->dw1.init_buf_full_in_bits < (unsigned int)(input_bits_per_frame * 2)) {
        pcmd->dw1.init_buf_full_in_bits = (unsigned int)(input_bits_per_frame * 2);
    }

    if (pcmd->dw1.init_buf_full_in_bits > pcmd->dw2.buf_size_in_bits) {
        pcmd->dw1.init_buf_full_in_bits = pcmd->dw2.buf_size_in_bits;
    }

    bps_ratio = input_bits_per_frame / ((double)(pcmd->dw2.buf_size_in_bits) / 30);
    bps_ratio = (bps_ratio < 0.1) ? 0.1 : (bps_ratio > 3.5) ? 3.5 : bps_ratio;

    pcmd->dw9.frame_width_in_bytes = vp8_context->picture_width;
    pcmd->dw10.frame_height_in_bytes = vp8_context->picture_height;
    pcmd->dw10.avbr_accuracy = 30;
    pcmd->dw11.avbr_convergence = 150;
    pcmd->dw11.min_qp = pic_param->clamp_qindex_low;
    pcmd->dw12.max_qp = pic_param->clamp_qindex_high;
    pcmd->dw12.level_qp = 60;

    // DW13 default 100
    pcmd->dw13.max_section_pct = 100;
    pcmd->dw13.under_shoot_cbr_pct = 115;

    // DW14 default 100
    pcmd->dw14.min_section_pct = 100;
    pcmd->dw14.vbr_bias_pct = 100;
    pcmd->dw15.instant_rate_threshold_0_for_p = 30;
    pcmd->dw15.instant_rate_threshold_1_for_p = 50;
    pcmd->dw15.instant_rate_threshold_2_for_p = 70;
    pcmd->dw15.instant_rate_threshold_3_for_p = 120;

    pcmd->dw17.instant_rate_threshold_0_for_i = 30;
    pcmd->dw17.instant_rate_threshold_1_for_i = 50;
    pcmd->dw17.instant_rate_threshold_2_for_i = 90;
    pcmd->dw17.instant_rate_threshold_3_for_i = 115;
    pcmd->dw18.deviation_threshold_0_for_p = (unsigned int)(-50 * pow(0.9, bps_ratio));
    pcmd->dw18.deviation_threshold_1_for_p = (unsigned int)(-50 * pow(0.66, bps_ratio));
    pcmd->dw18.deviation_threshold_2_for_p = (unsigned int)(-50 * pow(0.46, bps_ratio));
    pcmd->dw18.deviation_threshold_3_for_p = (unsigned int)(-50 * pow(0.3, bps_ratio));
    pcmd->dw19.deviation_threshold_4_for_p = (unsigned int)(50 * pow(0.3, bps_ratio));
    pcmd->dw19.deviation_threshold_5_for_p = (unsigned int)(50 * pow(0.46, bps_ratio));
    pcmd->dw19.deviation_threshold_6_for_p = (unsigned int)(50 * pow(0.7, bps_ratio));
    pcmd->dw19.deviation_threshold_7_for_p = (unsigned int)(50 * pow(0.9, bps_ratio));
    pcmd->dw20.deviation_threshold_0_for_vbr = (unsigned int)(-50 * pow(0.9, bps_ratio));
    pcmd->dw20.deviation_threshold_1_for_vbr = (unsigned int)(-50 * pow(0.7, bps_ratio));
    pcmd->dw20.deviation_threshold_2_for_vbr = (unsigned int)(-50 * pow(0.5, bps_ratio));
    pcmd->dw20.deviation_threshold_3_for_vbr = (unsigned int)(-50 * pow(0.3, bps_ratio));
    pcmd->dw21.deviation_threshold_4_for_vbr = (unsigned int)(100 * pow(0.4, bps_ratio));
    pcmd->dw21.deviation_threshold_5_for_vbr = (unsigned int)(100 * pow(0.5, bps_ratio));
    pcmd->dw21.deviation_threshold_6_for_vbr = (unsigned int)(100 * pow(0.75, bps_ratio));
    pcmd->dw21.deviation_threshold_7_for_vbr = (unsigned int)(100 * pow(0.9, bps_ratio));
    pcmd->dw22.deviation_threshold_0_for_i = (unsigned int)(-50 * pow(0.8, bps_ratio));
    pcmd->dw22.deviation_threshold_1_for_i = (unsigned int)(-50 * pow(0.6, bps_ratio));
    pcmd->dw22.deviation_threshold_2_for_i = (unsigned int)(-50 * pow(0.34, bps_ratio));
    pcmd->dw22.deviation_threshold_3_for_i = (unsigned int)(-50 * pow(0.2, bps_ratio));
    pcmd->dw23.deviation_threshold_4_for_i = (unsigned int)(50 * pow(0.2, bps_ratio));
    pcmd->dw23.deviation_threshold_5_for_i = (unsigned int)(50 * pow(0.4, bps_ratio));
    pcmd->dw23.deviation_threshold_6_for_i = (unsigned int)(50 * pow(0.66, bps_ratio));
    pcmd->dw23.deviation_threshold_7_for_i = (unsigned int)(50 * pow(0.9, bps_ratio));

    // Default: 1
    pcmd->dw24.num_t_levels = 1;

    if (!vp8_context->brc_initted) {
        vp8_context->brc_init_current_target_buf_full_in_bits = pcmd->dw1.init_buf_full_in_bits;
    }

    vp8_context->brc_init_reset_buf_size_in_bits = pcmd->dw2.buf_size_in_bits;
    vp8_context->brc_init_reset_input_bits_per_frame = input_bits_per_frame;

    pcmd->dw26.history_buffer_bti = VP8_BTI_BRC_INIT_RESET_HISTORY;
    pcmd->dw27.distortion_buffer_bti = VP8_BTI_BRC_INIT_RESET_DISTORTION;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_brc_init_reset_add_surfaces(VADriverContextP ctx,
                                                 struct encode_state *encode_state,
                                                 struct intel_encoder_context *encoder_context,
                                                 struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_history_buffer,
                                0,
                                vp8_context->brc_history_buffer.size,
                                0,
                                VP8_BTI_BRC_INIT_RESET_HISTORY);

    i965_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &vp8_context->brc_distortion_buffer,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   VP8_BTI_BRC_INIT_RESET_DISTORTION);
}

static VAStatus
i965_encoder_vp8_vme_brc_init_reset(VADriverContextP ctx,
                                    struct encode_state *encode_state,
                                    struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_brc_init_reset_context *init_reset_context = &vp8_context->brc_init_reset_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_media_object_parameter media_object_param;
    struct i965_gpe_context *gpe_context;
    int gpe_index = VP8_BRC_INIT;
    int media_function = VP8_MEDIA_STATE_BRC_INIT_RESET;

    if (vp8_context->brc_initted)
        gpe_index = VP8_BRC_RESET;

    gpe_context = &init_reset_context->gpe_contexts[gpe_index];

    gpe->context_init(ctx, gpe_context);
    gpe->reset_binding_table(ctx, gpe_context);
    i965_encoder_vp8_vme_brc_init_reset_set_curbe(ctx, encode_state, encoder_context, gpe_context);
    i965_encoder_vp8_vme_brc_init_reset_add_surfaces(ctx, encode_state, encoder_context, gpe_context);
    gpe->setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    i965_run_kernel_media_object(ctx, encoder_context, gpe_context, media_function, &media_object_param);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_scaling_set_curbe(VADriverContextP ctx,
                                       struct encode_state *encode_state,
                                       struct intel_encoder_context *encoder_context,
                                       struct i965_gpe_context *gpe_context,
                                       struct scaling_curbe_parameters *params)
{
    struct vp8_scaling_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw0.input_picture_width = params->input_picture_width;
    pcmd->dw0.input_picture_height = params->input_picture_height;

    if (!params->is_field_picture) {
        pcmd->dw1.input_y_bti_frame = VP8_BTI_SCALING_FRAME_SRC_Y;
        pcmd->dw2.output_y_bti_frame = VP8_BTI_SCALING_FRAME_DST_Y;
    } else {
        pcmd->dw1.input_y_bti_top_field = VP8_BTI_SCALING_FIELD_TOP_SRC_Y;
        pcmd->dw2.output_y_bti_top_field = VP8_BTI_SCALING_FIELD_TOP_DST_Y;
        pcmd->dw3.input_y_bti_bottom_field = VP8_BTI_SCALING_FIELD_BOT_SRC_Y;
        pcmd->dw4.output_y_bti_bottom_field = VP8_BTI_SCALING_FIELD_BOT_DST_Y;
    }

    if (params->flatness_check_enabled) {
        pcmd->dw5.flatness_threshold = 128;
        pcmd->dw6.enable_mb_flatness_check = 1;

        if (!params->is_field_picture) {
            pcmd->dw8.flatness_output_bti_frame = VP8_BTI_SCALING_FRAME_FLATNESS_DST;
        } else {
            pcmd->dw8.flatness_output_bti_top_field = VP8_BTI_SCALING_FIELD_TOP_FLATNESS_DST;
            pcmd->dw9.flatness_output_bti_bottom_field = VP8_BTI_SCALING_FIELD_BOT_FLATNESS_DST;
        }
    } else {
        pcmd->dw6.enable_mb_flatness_check = 0;
    }

    pcmd->dw6.enable_mb_variance_output = params->mb_variance_output_enabled;
    pcmd->dw6.enable_mb_pixel_average_output = params->mb_pixel_average_output_enabled;

    if (params->mb_variance_output_enabled || params->mb_pixel_average_output_enabled) {
        if (!params->is_field_picture) {
            pcmd->dw10.mbv_proc_stats_bti_frame = VP8_BTI_SCALING_FRAME_MBVPROCSTATS_DST;
        } else {
            pcmd->dw10.mbv_proc_stats_bti_top_field = VP8_BTI_SCALING_FIELD_TOP_MBVPROCSTATS_DST;
            pcmd->dw11.mbv_proc_stats_bti_bottom_field = VP8_BTI_SCALING_FIELD_BOT_MBVPROCSTATS_DST;
        }
    }

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_scaling_add_surfaces(VADriverContextP ctx,
                                          struct encode_state *encode_state,
                                          struct intel_encoder_context *encoder_context,
                                          struct i965_gpe_context *gpe_context,
                                          struct scaling_surface_parameters *params)
{
    i965_add_2d_gpe_surface(ctx,
                            gpe_context,
                            params->input_obj_surface,
                            0,
                            1,
                            I965_SURFACEFORMAT_R32_UNORM,
                            VP8_BTI_SCALING_FRAME_SRC_Y);
    i965_add_2d_gpe_surface(ctx,
                            gpe_context,
                            params->output_obj_surface,
                            0,
                            1,
                            I965_SURFACEFORMAT_R32_UNORM,
                            VP8_BTI_SCALING_FRAME_DST_Y);
}

static VAStatus
i965_encoder_vp8_vme_scaling(VADriverContextP ctx,
                             struct encode_state *encode_state,
                             struct intel_encoder_context *encoder_context,
                             int scaling_16x_enabled)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_scaling_context *scaling_context = &vp8_context->scaling_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct i965_gpe_context *gpe_context;
    struct scaling_curbe_parameters scaling_curbe_params;
    struct scaling_surface_parameters scaling_surface_params;
    struct vp8_encoder_kernel_walker_parameter kernel_walker_param;
    struct object_surface *input_obj_surface, *output_obj_surface;
    struct i965_encoder_vp8_surface *vp8_surface;
    unsigned int input_frame_width, input_frame_height, output_frame_width, output_frame_height;
    unsigned int down_scaled_width_in_mbs, down_scaled_height_in_mbs;
    int gpe_index, media_function;

    vp8_surface = encode_state->reconstructed_object->private_data;

    if (scaling_16x_enabled) {
        gpe_index = VP8_SCALING_16X;
        media_function = VP8_MEDIA_STATE_16X_SCALING;

        down_scaled_width_in_mbs = vp8_context->down_scaled_width_in_mb16x;
        down_scaled_height_in_mbs = vp8_context->down_scaled_height_in_mb16x;

        input_obj_surface = vp8_surface->scaled_4x_surface_obj;
        input_frame_width = vp8_context->down_scaled_width_4x;
        input_frame_height = vp8_context->down_scaled_height_4x;

        output_obj_surface = vp8_surface->scaled_16x_surface_obj;
        output_frame_width = vp8_context->down_scaled_width_16x;
        output_frame_height = vp8_context->down_scaled_height_16x;
    } else {
        gpe_index = VP8_SCALING_4X;
        media_function = VP8_MEDIA_STATE_4X_SCALING;

        down_scaled_width_in_mbs = vp8_context->down_scaled_width_in_mb4x;
        down_scaled_height_in_mbs = vp8_context->down_scaled_height_in_mb4x;

        input_obj_surface = encode_state->input_yuv_object;
        input_frame_width = vp8_context->picture_width;         /* the orignal width */
        input_frame_height = vp8_context->picture_height;       /* the orignal height */

        output_obj_surface = vp8_surface->scaled_4x_surface_obj;
        output_frame_width = vp8_context->down_scaled_width_4x;
        output_frame_height = vp8_context->down_scaled_height_4x;
    }

    gpe_context = &scaling_context->gpe_contexts[gpe_index];

    gpe->context_init(ctx, gpe_context);
    gpe->reset_binding_table(ctx, gpe_context);

    memset(&scaling_curbe_params, 0, sizeof(scaling_curbe_params));
    scaling_curbe_params.input_picture_width = input_frame_width;
    scaling_curbe_params.input_picture_height = input_frame_height;
    scaling_curbe_params.is_field_picture = 0;
    scaling_curbe_params.flatness_check_enabled = 0;
    scaling_curbe_params.mb_variance_output_enabled = 0;
    scaling_curbe_params.mb_pixel_average_output_enabled = 0;
    i965_encoder_vp8_vme_scaling_set_curbe(ctx, encode_state, encoder_context, gpe_context, &scaling_curbe_params);

    scaling_surface_params.input_obj_surface = input_obj_surface;
    scaling_surface_params.input_width = input_frame_width;
    scaling_surface_params.input_height = input_frame_height;
    scaling_surface_params.output_obj_surface = output_obj_surface;
    scaling_surface_params.output_width = output_frame_width;
    scaling_surface_params.output_height = output_frame_height;
    i965_encoder_vp8_vme_scaling_add_surfaces(ctx, encode_state, encoder_context, gpe_context, &scaling_surface_params);

    gpe->setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    kernel_walker_param.resolution_x = down_scaled_width_in_mbs * 2; /* 8x8 level */
    kernel_walker_param.resolution_y = down_scaled_height_in_mbs * 2;
    kernel_walker_param.no_dependency = 1;
    i965_init_media_object_walker_parameters(encoder_context, &kernel_walker_param, &media_object_walker_param);

    i965_run_kernel_media_object_walker(ctx, encoder_context, gpe_context, media_function, &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_me_set_curbe(VADriverContextP ctx,
                                  struct encode_state *encode_state,
                                  struct intel_encoder_context *encoder_context,
                                  struct i965_gpe_context *gpe_context,
                                  struct me_curbe_parameters *params)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_me_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    int me_mode, me_method;

    if (!pcmd)
        return;

    if (vp8_context->hme_16x_enabled) {
        if (params->use_16x_me)
            me_mode = VP8_ME_MODE_ME16X_BEFORE_ME4X;
        else
            me_mode = VP8_ME_MODE_ME4X_AFTER_ME16X;
    } else {
        me_mode = VP8_ME_MODE_ME4X_ONLY;
    }

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw1.max_num_mvs = 0x10;
    pcmd->dw1.bi_weight = 0;

    pcmd->dw2.max_num_su = 57;
    pcmd->dw2.max_len_sp = 57;

    pcmd->dw3.sub_mb_part_mask = 0x77;
    pcmd->dw3.inter_sad = 0;
    pcmd->dw3.intra_sad = 0;
    pcmd->dw3.bme_disable_fbr = 1;
    pcmd->dw3.sub_pel_mode = 3;

    pcmd->dw4.picture_height_minus1 = params->down_scaled_height_in_mbs - 1;
    pcmd->dw4.picture_width = params->down_scaled_width_in_mbs;

    if (pcmd->dw4.picture_height_minus1 < 2)
        pcmd->dw4.picture_height_minus1 = 2;

    if (pcmd->dw4.picture_width < 3)
        pcmd->dw4.picture_width = 3;

    pcmd->dw5.ref_height = 40;
    pcmd->dw5.ref_width = 48;

    pcmd->dw6.me_mode = me_mode;

    if (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY)
        pcmd->dw6.super_combine_dist = 5;
    else if (encoder_context->quality_level == ENCODER_LOW_QUALITY)
        pcmd->dw6.super_combine_dist = 0;
    else
        pcmd->dw6.super_combine_dist = 1;

    pcmd->dw6.max_vmv_range = 0x7fc;

    pcmd->dw13.num_ref_idx_l0_minus1 = vp8_num_refs[vp8_context->ref_frame_ctrl] - 1;
    pcmd->dw13.num_ref_idx_l1_minus1 = 0;

    me_method = (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) ? 6 : 4;
    memcpy(&pcmd->dw16, vp8_search_path[me_method], 14 * sizeof(pcmd->dw16));

    pcmd->dw32.vp8_me_mv_output_data_bti = VP8_BTI_ME_MV_DATA;
    pcmd->dw33.vp8_me_mv_input_data_bti = VP8_BTI_16X_ME_MV_DATA;
    pcmd->dw34.vp8_me_distorion_bti = VP8_BTI_ME_DISTORTION;
    pcmd->dw35.vp8_me_min_dist_brc_bti = VP8_BTI_ME_MIN_DIST_BRC_DATA;
    pcmd->dw36.vp8_me_forward_ref_bti = VP8_BTI_VME_INTER_PRED;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_me_add_surfaces(VADriverContextP ctx,
                                     struct encode_state *encode_state,
                                     struct intel_encoder_context *encoder_context,
                                     struct i965_gpe_context *gpe_context,
                                     struct me_surface_parameters *params)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_surface *vp8_surface;
    struct i965_gpe_resource *me_gpe_buffer, *me_brc_distortion_buffer;
    struct object_surface *obj_surface;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    if (brc_enabled)
        me_brc_distortion_buffer = &vp8_context->brc_distortion_buffer;
    else
        me_brc_distortion_buffer = &vp8_context->me_4x_distortion_buffer;

    if (params->use_16x_me) {
        me_gpe_buffer = &vp8_context->me_16x_mv_data_buffer;
    } else {
        me_gpe_buffer = &vp8_context->me_4x_mv_data_buffer;
    }

    i965_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   me_gpe_buffer,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   VP8_BTI_ME_MV_DATA);

    if (vp8_context->hme_16x_enabled) {
        me_gpe_buffer = &vp8_context->me_16x_mv_data_buffer;
        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       me_gpe_buffer,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_16X_ME_MV_DATA);
    }

    if (!params->use_16x_me) {
        me_gpe_buffer = &vp8_context->me_4x_distortion_buffer;
        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       me_gpe_buffer,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_ME_DISTORTION);

        me_gpe_buffer = me_brc_distortion_buffer;
        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       me_gpe_buffer,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_ME_MIN_DIST_BRC_DATA);
    }

    vp8_surface = encode_state->reconstructed_object->private_data;
    assert(vp8_surface);

    if (params->use_16x_me) {
        obj_surface = vp8_surface->scaled_16x_surface_obj;
    } else {
        obj_surface = vp8_surface->scaled_4x_surface_obj;
    }

    i965_add_adv_gpe_surface(ctx,
                             gpe_context,
                             obj_surface,
                             VP8_BTI_VME_INTER_PRED);

    if (vp8_context->ref_last_frame != NULL &&
        vp8_context->ref_last_frame->bo != NULL) {
        vp8_surface = vp8_context->ref_last_frame->private_data;
        obj_surface = NULL;

        if (vp8_surface) {
            if (params->use_16x_me) {
                obj_surface = vp8_surface->scaled_16x_surface_obj;
            } else {
                obj_surface = vp8_surface->scaled_4x_surface_obj;
            }
        }

        if (obj_surface) {
            i965_add_adv_gpe_surface(ctx,
                                     gpe_context,
                                     obj_surface,
                                     VP8_BTI_ME_REF1_PIC);
        }
    }

    if (vp8_context->ref_gf_frame != NULL &&
        vp8_context->ref_gf_frame->bo != NULL) {
        vp8_surface = vp8_context->ref_gf_frame->private_data;
        obj_surface = NULL;

        if (vp8_surface) {
            if (params->use_16x_me) {
                obj_surface = vp8_surface->scaled_16x_surface_obj;
            } else {
                obj_surface = vp8_surface->scaled_4x_surface_obj;
            }
        }

        if (obj_surface) {
            switch (vp8_context->ref_frame_ctrl) {
            case 2:
            case 6:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_ME_REF1_PIC);
                break;

            case 3:
            case 7:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_ME_REF2_PIC);
                break;
            }
        }
    }

    if (vp8_context->ref_arf_frame != NULL &&
        vp8_context->ref_arf_frame->bo != NULL) {
        vp8_surface = vp8_context->ref_arf_frame->private_data;
        obj_surface = NULL;

        if (vp8_surface) {
            if (params->use_16x_me) {
                obj_surface = vp8_surface->scaled_16x_surface_obj;
            } else {
                obj_surface = vp8_surface->scaled_4x_surface_obj;
            }
        }

        if (obj_surface) {
            switch (vp8_context->ref_frame_ctrl) {
            case 4:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_ME_REF1_PIC);
                break;

            case 5:
            case 6:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_ME_REF2_PIC);
                break;

            case 7:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_ME_REF3_PIC);
                break;
            }
        }
    }
}

static void
i965_encoder_vp8_vme_init_brc_distorion_buffer(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    i965_zero_gpe_resource(&vp8_context->brc_distortion_buffer);
}

static VAStatus
i965_encoder_vp8_vme_me(VADriverContextP ctx,
                        struct encode_state *encode_state,
                        struct intel_encoder_context *encoder_context,
                        int use_16x_me)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_me_context *me_context = &vp8_context->me_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct vp8_encoder_kernel_walker_parameter kernel_walker_params;
    struct me_curbe_parameters me_curbe_params;
    struct i965_gpe_context *gpe_context;
    struct me_surface_parameters me_surface_params;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    unsigned int down_scaled_width_in_mbs, down_scaled_height_in_mbs;
    unsigned int ref_frame_flag_final, ref_frame_flag;
    int gpe_index, media_function;

    if (vp8_context->frame_type == MPEG_P_PICTURE) {
        ref_frame_flag = VP8_REF_FLAG_ALL;

        if (pic_param->ref_last_frame == pic_param->ref_gf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_GOLDEN;
        }

        if (pic_param->ref_last_frame == pic_param->ref_arf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_ALT;
        }

        if (pic_param->ref_gf_frame == pic_param->ref_arf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_ALT;
        }
    } else {
        ref_frame_flag = VP8_REF_FLAG_LAST;
    }

    switch (vp8_context->ref_frame_ctrl) {
    case 0:
        ref_frame_flag_final = VP8_REF_FLAG_NONE;
        break;

    case 1:
        ref_frame_flag_final = VP8_REF_FLAG_LAST;       // Last Ref only
        break;

    case 2:
        ref_frame_flag_final = VP8_REF_FLAG_GOLDEN;     // Gold Ref only
        break;

    case 4:
        ref_frame_flag_final = VP8_REF_FLAG_ALT;        // Alt Ref only
        break;

    default:
        ref_frame_flag_final = ref_frame_flag;
    }

    vp8_context->ref_frame_ctrl = ref_frame_flag_final;
    vp8_context->ref_ctrl_optimization_done = 1;

    if (use_16x_me) {
        gpe_index = VP8_ME_16X;
        media_function = VP8_MEDIA_STATE_16X_ME;
        down_scaled_width_in_mbs = vp8_context->down_scaled_width_in_mb16x;
        down_scaled_height_in_mbs = vp8_context->down_scaled_height_in_mb16x;
    } else {
        gpe_index = VP8_ME_4X;
        media_function = VP8_MEDIA_STATE_4X_ME;
        down_scaled_width_in_mbs = vp8_context->down_scaled_width_in_mb4x;
        down_scaled_height_in_mbs = vp8_context->down_scaled_height_in_mb4x;
    }

    gpe_context = &me_context->gpe_contexts[gpe_index];

    gpe->context_init(ctx, gpe_context);
    gpe->reset_binding_table(ctx, gpe_context);

    memset(&me_curbe_params, 0, sizeof(me_curbe_params));
    me_curbe_params.down_scaled_width_in_mbs = down_scaled_width_in_mbs;
    me_curbe_params.down_scaled_height_in_mbs = down_scaled_height_in_mbs;
    me_curbe_params.use_16x_me = use_16x_me;
    i965_encoder_vp8_vme_me_set_curbe(ctx, encode_state, encoder_context, gpe_context, &me_curbe_params);

    if (vp8_context->brc_distortion_buffer_need_reset && !use_16x_me) {
        i965_encoder_vp8_vme_init_brc_distorion_buffer(ctx, encoder_context);
    }

    memset(&me_surface_params, 0, sizeof(me_surface_params));
    me_surface_params.use_16x_me = use_16x_me;
    i965_encoder_vp8_vme_me_add_surfaces(ctx, encode_state, encoder_context, gpe_context, &me_surface_params);

    gpe->setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_params, 0, sizeof(kernel_walker_params));
    kernel_walker_params.resolution_x = down_scaled_width_in_mbs;
    kernel_walker_params.resolution_y = down_scaled_height_in_mbs;
    kernel_walker_params.no_dependency = 1;
    i965_init_media_object_walker_parameters(encoder_context, &kernel_walker_params, &media_object_walker_param);

    i965_run_kernel_media_object_walker(ctx, encoder_context, gpe_context, media_function, &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

#define QUANT_INDEX(index, q_index, q_index_delta)                      \
    do {                                                                \
        index = quant_param->quantization_index[q_index] + quant_param->quantization_index_delta[q_index_delta]; \
        index = MIN(MAX_QP_VP8, index);                            \
    } while (0)

static void
i965_encoder_vp8_vme_mbenc_set_i_frame_curbe(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context,
                                             struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_mbenc_i_frame_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    unsigned int segmentation_enabled = pic_param->pic_flags.bits.segmentation_enabled;
    unsigned short y_quanta_dc_idx, uv_quanta_dc_idx, uv_quanta_ac_idx;

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw0.frame_width = vp8_context->frame_width;
    pcmd->dw0.frame_height = vp8_context->frame_height;

    pcmd->dw1.frame_type = 0; /* key frame */
    pcmd->dw1.enable_segmentation = segmentation_enabled;
    pcmd->dw1.enable_hw_intra_prediction = (encoder_context->quality_level == ENCODER_LOW_QUALITY) ? 1 : 0;
    pcmd->dw1.enable_chroma_ip_enhancement = 1; /* Cannot be disabled */
    pcmd->dw1.enable_debug_dumps = 0;
    pcmd->dw1.enable_mpu_histogram_update = 1;
    pcmd->dw1.vme_distortion_measure = 2; /* HAAR transform */
    pcmd->dw1.vme_enable_tm_check = 0;

    QUANT_INDEX(y_quanta_dc_idx, 0, 0);
    pcmd->dw2.lambda_seg_0 = (unsigned short)((quant_dc_vp8[y_quanta_dc_idx] * quant_dc_vp8[y_quanta_dc_idx]) / 4);

    if (segmentation_enabled) {
        QUANT_INDEX(y_quanta_dc_idx, 1, 0);
        pcmd->dw2.lambda_seg_1 = (unsigned short)((quant_dc_vp8[y_quanta_dc_idx] * quant_dc_vp8[y_quanta_dc_idx]) / 4);

        QUANT_INDEX(y_quanta_dc_idx, 2, 0);
        pcmd->dw3.lambda_seg_2 = (unsigned short)((quant_dc_vp8[y_quanta_dc_idx] * quant_dc_vp8[y_quanta_dc_idx]) / 4);

        QUANT_INDEX(y_quanta_dc_idx, 3, 0);
        pcmd->dw3.lambda_seg_3 = (unsigned short)((quant_dc_vp8[y_quanta_dc_idx] * quant_dc_vp8[y_quanta_dc_idx]) / 4);
    }

    pcmd->dw4.all_dc_bias_segment_0 = DC_BIAS_SEGMENT_DEFAULT_VAL_VP8;

    if (segmentation_enabled) {
        pcmd->dw4.all_dc_bias_segment_1 = DC_BIAS_SEGMENT_DEFAULT_VAL_VP8;
        pcmd->dw5.all_dc_bias_segment_2 = DC_BIAS_SEGMENT_DEFAULT_VAL_VP8;
        pcmd->dw5.all_dc_bias_segment_3 = DC_BIAS_SEGMENT_DEFAULT_VAL_VP8;
    }

    QUANT_INDEX(uv_quanta_dc_idx, 0, 1);
    pcmd->dw6.chroma_dc_de_quant_segment_0 = quant_dc_vp8[uv_quanta_dc_idx];

    if (segmentation_enabled) {
        QUANT_INDEX(uv_quanta_dc_idx, 1, 1);
        pcmd->dw6.chroma_dc_de_quant_segment_1 = quant_dc_vp8[uv_quanta_dc_idx];

        QUANT_INDEX(uv_quanta_dc_idx, 2, 1);
        pcmd->dw7.chroma_dc_de_quant_segment_2 = quant_dc_vp8[uv_quanta_dc_idx];

        QUANT_INDEX(uv_quanta_dc_idx, 3, 1);
        pcmd->dw7.chroma_dc_de_quant_segment_3 = quant_dc_vp8[uv_quanta_dc_idx];
    }

    QUANT_INDEX(uv_quanta_ac_idx, 0, 2);
    pcmd->dw8.chroma_ac_de_quant_segment0 = quant_ac_vp8[uv_quanta_ac_idx];
    pcmd->dw10.chroma_ac0_threshold0_segment0 = (unsigned short)((((((1) << 16) -
                                                                    1) * 1.0 / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                   ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                  (1 << 13) + 3400) / 2217.0);
    pcmd->dw10.chroma_ac0_threshold1_segment0 = (unsigned short)((((((2) << 16) -
                                                                    1) * 1.0 / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                   ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                  (1 << 13) + 3400) / 2217.0);

    if (segmentation_enabled) {
        QUANT_INDEX(uv_quanta_ac_idx, 1, 2);
        pcmd->dw8.chroma_ac_de_quant_segment1 = quant_ac_vp8[uv_quanta_ac_idx];
        pcmd->dw10.chroma_ac0_threshold0_segment0 = (unsigned short)((((((1) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);
        pcmd->dw10.chroma_ac0_threshold1_segment0 = (unsigned short)((((((2) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);

        QUANT_INDEX(uv_quanta_ac_idx, 2, 2);
        pcmd->dw9.chroma_ac_de_quant_segment2 = quant_ac_vp8[uv_quanta_ac_idx];
        pcmd->dw12.chroma_ac0_threshold0_segment2 = (unsigned short)((((((1) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);
        pcmd->dw12.chroma_ac0_threshold1_segment2 = (unsigned short)((((((2) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);

        QUANT_INDEX(uv_quanta_ac_idx, 3, 2);
        pcmd->dw9.chroma_ac_de_quant_segment3 = quant_ac_vp8[uv_quanta_ac_idx];
        pcmd->dw13.chroma_ac0_threshold0_segment3 = (unsigned short)((((((1) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);
        pcmd->dw13.chroma_ac0_threshold1_segment3 = (unsigned short)((((((2) << 16) -
                                                                        1) * 1.0 / ((1 << 16) /
                                                                                    quant_ac_vp8[uv_quanta_ac_idx]) -
                                                                       ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7)) *
                                                                      (1 << 13) + 3400) / 2217.0);
    }

    QUANT_INDEX(uv_quanta_dc_idx, 0, 1);
    pcmd->dw14.chroma_dc_threshold0_segment0 = (((1) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                               ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
    pcmd->dw14.chroma_dc_threshold1_segment0 = (((2) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                               ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
    pcmd->dw15.chroma_dc_threshold2_segment0 = (((3) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                               ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
    pcmd->dw15.chroma_dc_threshold3_segment0 = (((4) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                               ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);

    if (segmentation_enabled) {
        QUANT_INDEX(uv_quanta_dc_idx, 1, 1);
        pcmd->dw16.chroma_dc_threshold0_segment1 = (((1) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw16.chroma_dc_threshold1_segment1 = (((2) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw17.chroma_dc_threshold2_segment1 = (((3) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw17.chroma_dc_threshold3_segment1 = (((4) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);

        QUANT_INDEX(uv_quanta_dc_idx, 2, 1);
        pcmd->dw18.chroma_dc_threshold0_segment2 = (((1) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw18.chroma_dc_threshold1_segment2 = (((2) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw19.chroma_dc_threshold2_segment2 = (((3) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw19.chroma_dc_threshold3_segment2 = (((4) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);

        QUANT_INDEX(uv_quanta_dc_idx, 3, 1);
        pcmd->dw20.chroma_dc_threshold0_segment3 = (((1) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw20.chroma_dc_threshold1_segment3 = (((2) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw21.chroma_dc_threshold2_segment3 = (((3) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
        pcmd->dw21.chroma_dc_threshold3_segment3 = (((4) << 16) - 1) / ((1 << 16) / quant_dc_vp8[uv_quanta_dc_idx]) -
                                                   ((48 * quant_dc_vp8[uv_quanta_dc_idx]) >> 7);
    }

    QUANT_INDEX(uv_quanta_ac_idx, 0, 2);
    pcmd->dw22.chroma_ac1_threshold_segment0 = ((1 << (16)) - 1) / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
                                               ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7);

    if (segmentation_enabled) {
        QUANT_INDEX(uv_quanta_ac_idx, 1, 2);
        pcmd->dw22.chroma_ac1_threshold_segment1 = ((1 << (16)) - 1) / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
                                                   ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7);

        QUANT_INDEX(uv_quanta_ac_idx, 2, 2);
        pcmd->dw23.chroma_ac1_threshold_segment2 = ((1 << (16)) - 1) / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
                                                   ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7);

        QUANT_INDEX(uv_quanta_ac_idx, 3, 2);
        pcmd->dw23.chroma_ac1_threshold_segment3 =
            ((1 << (16)) - 1) / ((1 << 16) / quant_ac_vp8[uv_quanta_ac_idx]) -
            ((48 * quant_ac_vp8[uv_quanta_ac_idx]) >> 7);
    }

    QUANT_INDEX(uv_quanta_dc_idx, 0, 0);
    pcmd->dw24.vme_16x16_cost_segment0 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][0];
    pcmd->dw25.vme_4x4_cost_segment0 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][1];
    pcmd->dw26.vme_16x16_non_dc_penalty_segment0 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][2];
    pcmd->dw27.vme_4x4_non_dc_penalty_segment0 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][3];

    if (segmentation_enabled) {
        QUANT_INDEX(uv_quanta_dc_idx, 1, 0);
        pcmd->dw24.vme_16x16_cost_segment1 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][0];
        pcmd->dw25.vme_4x4_cost_segment1 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][1];
        pcmd->dw26.vme_16x16_non_dc_penalty_segment1 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][2];
        pcmd->dw27.vme_4x4_non_dc_penalty_segment1 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][3];

        QUANT_INDEX(uv_quanta_dc_idx, 2, 0);
        pcmd->dw24.vme_16x16_cost_segment2 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][0];
        pcmd->dw25.vme_4x4_cost_segment2 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][1];
        pcmd->dw26.vme_16x16_non_dc_penalty_segment2 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][2];
        pcmd->dw27.vme_4x4_non_dc_penalty_segment2 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][3];

        QUANT_INDEX(uv_quanta_dc_idx, 3, 0);
        pcmd->dw24.vme_16x16_cost_segment3 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][0];
        pcmd->dw25.vme_4x4_cost_segment3 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][1];
        pcmd->dw26.vme_16x16_non_dc_penalty_segment3 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][2];
        pcmd->dw27.vme_4x4_non_dc_penalty_segment3 = i_frame_vme_costs_vp8[uv_quanta_dc_idx & 0x7F][3];
    }

    pcmd->dw32.mb_enc_per_mb_out_data_surf_bti = VP8_BTI_MBENC_PER_MB_OUT;
    pcmd->dw33.mb_enc_curr_y_bti = VP8_BTI_MBENC_CURR_Y;
    pcmd->dw34.mb_enc_curr_uv_bti = VP8_BTI_MBENC_CURR_Y;
    pcmd->dw35.mb_mode_cost_luma_bti = VP8_BTI_MBENC_MB_MODE_COST_LUMA;
    pcmd->dw36.mb_enc_block_mode_cost_bti = VP8_BTI_MBENC_BLOCK_MODE_COST;
    pcmd->dw37.chroma_recon_surf_bti = VP8_BTI_MBENC_CHROMA_RECON;
    pcmd->dw38.segmentation_map_bti = VP8_BTI_MBENC_SEGMENTATION_MAP;
    pcmd->dw39.histogram_bti = VP8_BTI_MBENC_HISTOGRAM;
    pcmd->dw40.mb_enc_vme_debug_stream_out_bti = VP8_BTI_MBENC_I_VME_DEBUG_STREAMOUT;
    pcmd->dw41.vme_bti = VP8_BTI_MBENC_VME;
    pcmd->dw42.idist_surface_bti = VP8_BTI_MBENC_IDIST;
    pcmd->dw43.curr_y_down_scaled_bti = VP8_BTI_MBENC_CURR_Y_DOWNSCALED;
    pcmd->dw44.vme_coarse_intra_bti = VP8_BTI_MBENC_VME_COARSE_INTRA;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_mbenc_set_p_frame_curbe(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context,
                                             struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_mbenc_p_frame_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    unsigned int segmentation_enabled = pic_param->pic_flags.bits.segmentation_enabled;
    unsigned short qp_seg0, qp_seg1, qp_seg2, qp_seg3;
    unsigned char me_method = (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) ? 6 : 4;

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    QUANT_INDEX(qp_seg0, 0, 0);
    QUANT_INDEX(qp_seg1, 0, 0);
    QUANT_INDEX(qp_seg2, 0, 0);
    QUANT_INDEX(qp_seg3, 3, 0);

    pcmd->dw0.frame_width = vp8_context->frame_width;
    pcmd->dw0.frame_height = vp8_context->frame_height;

    pcmd->dw1.frame_type = 1;   // P-frame
    pcmd->dw1.multiple_pred = (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) ? 1 :
                              ((encoder_context->quality_level == ENCODER_LOW_QUALITY) ? 0 : 2);
    pcmd->dw1.hme_enable = vp8_context->hme_enabled;
    pcmd->dw1.hme_combine_overlap = 1;
    pcmd->dw1.enable_temporal_scalability = 0;
    pcmd->dw1.ref_frame_flags = vp8_context->ref_frame_ctrl;
    pcmd->dw1.enable_segmentation = segmentation_enabled;
    pcmd->dw1.enable_segmentation_info_update = 1;
    pcmd->dw1.multi_reference_qp_check = 0;
    pcmd->dw1.mode_cost_enable_flag = 1;
    pcmd->dw1.main_ref = mainref_table_vp8[vp8_context->ref_frame_ctrl];

    pcmd->dw2.lambda_intra_segment0 = quant_dc_vp8[qp_seg0];
    pcmd->dw2.lambda_inter_segment0 = (quant_dc_vp8[qp_seg0] >> 2);

    pcmd->dw3.lambda_intra_segment1 = quant_dc_vp8[qp_seg1];
    pcmd->dw3.lambda_inter_segment1 = (quant_dc_vp8[qp_seg1] >> 2);

    pcmd->dw4.lambda_intra_segment2 = quant_dc_vp8[qp_seg2];
    pcmd->dw4.lambda_inter_segment2 = (quant_dc_vp8[qp_seg2] >> 2);

    pcmd->dw5.lambda_intra_segment3 = quant_dc_vp8[qp_seg3];
    pcmd->dw5.lambda_inter_segment3 = (quant_dc_vp8[qp_seg3] >> 2);

    pcmd->dw6.reference_frame_sign_bias_3 = pic_param->pic_flags.bits.sign_bias_golden;
    pcmd->dw6.reference_frame_sign_bias_2 = pic_param->pic_flags.bits.sign_bias_alternate;
    pcmd->dw6.reference_frame_sign_bias_1 = pic_param->pic_flags.bits.sign_bias_golden ^ pic_param->pic_flags.bits.sign_bias_alternate;
    pcmd->dw6.reference_frame_sign_bias_0 = 0;

    pcmd->dw7.raw_dist_threshold = (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) ? 50 :
                                   ((encoder_context->quality_level == ENCODER_LOW_QUALITY) ? 0 : 100);
    pcmd->dw7.temporal_layer_id = 0;

    pcmd->dw8.early_ime_successful_stop_threshold = 0;
    pcmd->dw8.adaptive_search_enable = (encoder_context->quality_level != ENCODER_LOW_QUALITY) ? 1 : 0;
    pcmd->dw8.skip_mode_enable = 1;
    pcmd->dw8.bidirectional_mix_disbale = 0;
    pcmd->dw8.transform8x8_flag_for_inter_enable = 0;
    pcmd->dw8.early_ime_success_enable = 0;

    pcmd->dw9.ref_pixel_bias_enable = 0;
    pcmd->dw9.unidirection_mix_enable = 0;
    pcmd->dw9.bidirectional_weight = 0;
    pcmd->dw9.ref_id_polarity_bits = 0;
    pcmd->dw9.max_num_of_motion_vectors = 0;

    pcmd->dw10.max_fixed_search_path_length = (encoder_context->quality_level == ENCODER_DEFAULT_QUALITY) ? 25 :
                                              ((encoder_context->quality_level == ENCODER_LOW_QUALITY) ? 9 : 57);
    pcmd->dw10.maximum_search_path_length = 57;

    pcmd->dw11.submacro_block_subPartition_mask = 0;
    pcmd->dw11.intra_sad_measure_adjustment = 2;
    pcmd->dw11.inter_sad_measure_adjustment = 2;
    pcmd->dw11.block_based_skip_enable = 0;
    pcmd->dw11.bme_disable_for_fbr_message = 0;
    pcmd->dw11.forward_trans_form_skip_check_enable = 0;
    pcmd->dw11.process_inter_chroma_pixels_mode = 0;
    pcmd->dw11.disable_field_cache_allocation = 0;
    pcmd->dw11.skip_mode_type = 0;
    pcmd->dw11.sub_pel_mode = 3;
    pcmd->dw11.dual_search_path_option = 0;
    pcmd->dw11.search_control = 0;
    pcmd->dw11.reference_access = 0;
    pcmd->dw11.source_access = 0;
    pcmd->dw11.inter_mb_type_road_map = 0;
    pcmd->dw11.source_block_size = 0;

    pcmd->dw12.reference_search_windows_height = (encoder_context->quality_level != ENCODER_LOW_QUALITY) ? 40 : 28;
    pcmd->dw12.reference_search_windows_width = (encoder_context->quality_level != ENCODER_LOW_QUALITY) ? 48 : 28;

    pcmd->dw13.mode_0_3_cost_seg0 = cost_table_vp8[qp_seg0][0];
    pcmd->dw14.mode_4_7_cost_seg0 = cost_table_vp8[qp_seg0][1];
    pcmd->dw15.mode_8_9_ref_id_chroma_cost_seg0 = cost_table_vp8[qp_seg0][2];

    switch (me_method) {
    case 2:
        memcpy(&(pcmd->dw16), single_su_vp8, sizeof(single_su_vp8));
        break;

    case 3:
        memcpy(&(pcmd->dw16), raster_scan_48x40_vp8, sizeof(raster_scan_48x40_vp8));
        break;

    case 4:
    case 5:
        memcpy(&(pcmd->dw16), full_spiral_48x40_vp8, sizeof(full_spiral_48x40_vp8));
        break;

    case 6:
    default:
        memcpy(&(pcmd->dw16), diamond_vp8, sizeof(diamond_vp8));
        break;
    }

    pcmd->dw30.mv_0_3_cost_seg0 = cost_table_vp8[qp_seg0][3];
    pcmd->dw31.mv_4_7_cost_seg0 = cost_table_vp8[qp_seg0][4];

    pcmd->dw32.bilinear_enable = 0;
    pcmd->dw32.intra_16x16_no_dc_penalty_segment0 = cost_table_vp8[qp_seg0][5];
    pcmd->dw32.intra_16x16_no_dc_penalty_segment1 = cost_table_vp8[qp_seg1][5];

    pcmd->dw33.intra_16x16_no_dc_penalty_segment2 = cost_table_vp8[qp_seg2][5];
    pcmd->dw33.intra_16x16_no_dc_penalty_segment3 = cost_table_vp8[qp_seg3][5];
    pcmd->dw33.hme_combine_len = 8;

    /* dw34 to dw 57 */
    memcpy(&(pcmd->dw34), mv_ref_cost_context_vp8, 24 * sizeof(unsigned int));

    pcmd->dw58.enc_cost_16x16 = 0;
    pcmd->dw58.enc_cost_16x8 = 0x73c;

    pcmd->dw59.enc_cost_8x8 = 0x365;
    pcmd->dw59.enc_cost_4x4 = 0xdc9;

    pcmd->dw60.frame_count_probability_ref_frame_cost_0 = 0x0204;
    pcmd->dw60.frame_count_probability_ref_frame_cost_1 = 0x006a;

    pcmd->dw61.frame_count_probability_ref_frame_cost_2 = 0x0967;
    pcmd->dw61.frame_count_probability_ref_frame_cost_3 = 0x0969;

    switch (vp8_context->frame_num % vp8_context->gop_size) {
    case 1:
        pcmd->dw62.average_qp_of_last_ref_frame = quant_dc_vp8[vp8_context->average_i_frame_qp];
        pcmd->dw62.average_qp_of_gold_ref_frame = pcmd->dw62.average_qp_of_last_ref_frame;
        pcmd->dw62.average_qp_of_alt_ref_frame  = pcmd->dw62.average_qp_of_last_ref_frame;
        break;

    case 2:
        pcmd->dw62.average_qp_of_last_ref_frame = quant_dc_vp8[vp8_context->average_p_frame_qp];
        pcmd->dw62.average_qp_of_gold_ref_frame = quant_dc_vp8[vp8_context->average_i_frame_qp];
        pcmd->dw62.average_qp_of_alt_ref_frame  = pcmd->dw62.average_qp_of_gold_ref_frame;
        break;

    case 3:
        pcmd->dw62.average_qp_of_last_ref_frame = quant_dc_vp8[vp8_context->average_p_frame_qp];
        pcmd->dw62.average_qp_of_gold_ref_frame = quant_dc_vp8[vp8_context->average_p_frame_qp];
        pcmd->dw62.average_qp_of_alt_ref_frame  = quant_dc_vp8[vp8_context->average_i_frame_qp];
        break;

    default:
        pcmd->dw62.average_qp_of_last_ref_frame = quant_dc_vp8[vp8_context->average_p_frame_qp];
        pcmd->dw62.average_qp_of_gold_ref_frame = pcmd->dw62.average_qp_of_last_ref_frame;
        pcmd->dw62.average_qp_of_alt_ref_frame  = pcmd->dw62.average_qp_of_last_ref_frame;
        break;
    }

    pcmd->dw63.intra_4x4_no_dc_penalty_segment0 = cost_table_vp8[qp_seg0][6];
    pcmd->dw63.intra_4x4_no_dc_penalty_segment1 = cost_table_vp8[qp_seg1][6];
    pcmd->dw63.intra_4x4_no_dc_penalty_segment2 = cost_table_vp8[qp_seg2][6];
    pcmd->dw63.intra_4x4_no_dc_penalty_segment3 = cost_table_vp8[qp_seg3][6];

    pcmd->dw64.mode_0_3_cost_seg1 = cost_table_vp8[qp_seg1][0];
    pcmd->dw65.mode_4_7_cost_seg1 = cost_table_vp8[qp_seg1][1];
    pcmd->dw66.mode_8_9_ref_id_chroma_cost_seg1 = cost_table_vp8[qp_seg1][2];

    pcmd->dw67.mv_0_3_cost_seg1 = cost_table_vp8[qp_seg1][3];
    pcmd->dw68.mv_4_7_cost_seg1 = cost_table_vp8[qp_seg1][4];

    pcmd->dw69.mode_0_3_cost_seg2 = cost_table_vp8[qp_seg2][0];
    pcmd->dw70.mode_4_7_cost_seg2 = cost_table_vp8[qp_seg2][1];
    pcmd->dw71.mode_8_9_ref_id_chroma_cost_seg2 = cost_table_vp8[qp_seg2][2];

    pcmd->dw72.mv_0_3_cost_seg2 = cost_table_vp8[qp_seg2][3];
    pcmd->dw73.mv_4_7_cost_seg2 = cost_table_vp8[qp_seg2][4];

    pcmd->dw74.mode_0_3_cost_seg3 = cost_table_vp8[qp_seg3][0];
    pcmd->dw75.mode_4_7_cost_seg3 = cost_table_vp8[qp_seg3][1];
    pcmd->dw76.mode_8_9_ref_id_chroma_cost_seg3 = cost_table_vp8[qp_seg3][2];

    pcmd->dw77.mv_0_3_cost_seg3 = cost_table_vp8[qp_seg3][3];
    pcmd->dw78.mv_4_7_cost_seg3 = cost_table_vp8[qp_seg3][4];

    pcmd->dw79.new_mv_skip_threshold_segment0 = new_mv_skip_threshold_vp8[qp_seg0];
    pcmd->dw79.new_mv_skip_threshold_segment1 = new_mv_skip_threshold_vp8[qp_seg1];
    pcmd->dw80.new_mv_skip_threshold_segment2 = new_mv_skip_threshold_vp8[qp_seg2];
    pcmd->dw80.new_mv_skip_threshold_segment3 = new_mv_skip_threshold_vp8[qp_seg3];

    pcmd->dw81.per_mb_output_data_surface_bti = VP8_BTI_MBENC_PER_MB_OUT;
    pcmd->dw82.current_picture_y_surface_bti = VP8_BTI_MBENC_CURR_Y;
    pcmd->dw83.current_picture_interleaved_uv_surface_bti = VP8_BTI_MBENC_CURR_Y;
    pcmd->dw84.hme_mv_data_surface_bti = VP8_BTI_MBENC_MV_DATA_FROM_ME;
    pcmd->dw85.mv_data_surface_bti = VP8_BTI_MBENC_IND_MV_DATA;
    pcmd->dw86.mb_count_per_reference_frame_bti = VP8_BTI_MBENC_REF_MB_COUNT;
    pcmd->dw87.vme_inter_prediction_bti = VP8_BTI_MBENC_INTER_PRED;
    pcmd->dw88.active_ref1_bti = VP8_BTI_MBENC_REF1_PIC;
    pcmd->dw89.active_ref2_bti = VP8_BTI_MBENC_REF2_PIC;
    pcmd->dw90.active_ref3_bti = VP8_BTI_MBENC_REF3_PIC;
    pcmd->dw91.per_mb_quant_data_bti = VP8_BTI_MBENC_P_PER_MB_QUANT;
    pcmd->dw92.segment_map_bti = VP8_BTI_MBENC_SEGMENTATION_MAP;
    pcmd->dw93.inter_prediction_distortion_bti = VP8_BTI_MBENC_INTER_PRED_DISTORTION;
    pcmd->dw94.histogram_bti = VP8_BTI_MBENC_HISTOGRAM;
    pcmd->dw95.pred_mv_data_bti = VP8_BTI_MBENC_PRED_MV_DATA;
    pcmd->dw96.mode_cost_update_bti = VP8_BTI_MBENC_MODE_COST_UPDATE;
    pcmd->dw97.kernel_debug_dump_bti = VP8_BTI_MBENC_P_VME_DEBUG_STREAMOUT;

    i965_gpe_context_unmap_curbe(gpe_context);
}

#undef QUANT_INDEX

static void
i965_encoder_vp8_vme_mbenc_set_curbe(VADriverContextP ctx,
                                     struct encode_state *encode_state,
                                     struct intel_encoder_context *encoder_context,
                                     struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    if (vp8_context->frame_type == MPEG_I_PICTURE)
        i965_encoder_vp8_vme_mbenc_set_i_frame_curbe(ctx, encode_state, encoder_context, gpe_context);
    else
        i965_encoder_vp8_vme_mbenc_set_p_frame_curbe(ctx, encode_state, encoder_context, gpe_context);
}

static void
i965_encoder_vp8_vme_mbenc_add_surfaces(VADriverContextP ctx,
                                        struct encode_state *encode_state,
                                        struct intel_encoder_context *encoder_context,
                                        struct i965_gpe_context *gpe_context,
                                        struct mbenc_surface_parameters *params)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_surface *vp8_surface;
    struct object_surface *obj_surface;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    unsigned int size = vp8_context->frame_width_in_mbs * vp8_context->frame_height_in_mbs * 16;
    unsigned int segmentation_enabled = pic_param->pic_flags.bits.segmentation_enabled;

    /* Per MB output data buffer */
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->mb_coded_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_MBENC_PER_MB_OUT);

    /* Current input surface Y & UV */
    i965_add_2d_gpe_surface(ctx,
                            gpe_context,
                            encode_state->input_yuv_object,
                            0,
                            1,
                            I965_SURFACEFORMAT_R8_UNORM,
                            VP8_BTI_MBENC_CURR_Y);

    i965_add_2d_gpe_surface(ctx,
                            gpe_context,
                            encode_state->input_yuv_object,
                            1,
                            1,
                            I965_SURFACEFORMAT_R8_UNORM,
                            VP8_BTI_MBENC_CURR_UV);

    /* Current surface for VME */
    i965_add_adv_gpe_surface(ctx,
                             gpe_context,
                             encode_state->input_yuv_object,
                             VP8_BTI_MBENC_VME);

    if (segmentation_enabled) {
        /* TODO check the internal segmetation buffer */
        dri_bo *bo = NULL;

        if (encode_state->encmb_map)
            bo = encode_state->encmb_map->bo;

        if (bo) {
            i965_add_dri_buffer_2d_gpe_surface(ctx,
                                               encoder_context,
                                               gpe_context,
                                               bo,
                                               vp8_context->frame_width_in_mbs,
                                               vp8_context->frame_height_in_mbs,
                                               vp8_context->frame_width_in_mbs,
                                               0,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               VP8_BTI_MBENC_SEGMENTATION_MAP);
        }
    }

    /* Histogram buffer */
    size = VP8_HISTOGRAM_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->histogram_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MBENC_HISTOGRAM);

    if (vp8_context->frame_type == MPEG_I_PICTURE) {
        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       &vp8_context->mb_mode_cost_luma_buffer,
                                       0,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_MBENC_MB_MODE_COST_LUMA);

        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       &vp8_context->block_mode_cost_buffer,
                                       0,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_MBENC_BLOCK_MODE_COST);

        /* Chroma recon buffer */
        size = vp8_context->frame_width_in_mbs * vp8_context->frame_height_in_mbs * 64;
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->chroma_recon_buffer,
                                    0,
                                    size,
                                    0,
                                    VP8_BTI_MBENC_CHROMA_RECON);

        if (params->i_frame_dist_in_use) {
            i965_add_buffer_2d_gpe_surface(ctx,
                                           gpe_context,
                                           params->me_brc_distortion_buffer,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           VP8_BTI_MBENC_IDIST);


            vp8_surface = encode_state->reconstructed_object->private_data;
            assert(vp8_surface);

            if (vp8_surface && vp8_surface->scaled_4x_surface_obj) {
                obj_surface = vp8_surface->scaled_4x_surface_obj;
            } else
                obj_surface = NULL;

            if (obj_surface) {
                i965_add_2d_gpe_surface(ctx,
                                        gpe_context,
                                        obj_surface,
                                        0,
                                        0,
                                        I965_SURFACEFORMAT_R8_UNORM,
                                        VP8_BTI_MBENC_CURR_Y_DOWNSCALED);

                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_VME_COARSE_INTRA);
            }
        }
    } else {
        size = vp8_context->frame_width_in_mbs * vp8_context->frame_height_in_mbs * 64;

        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->mb_coded_buffer,
                                    1,
                                    size,
                                    vp8_context->mv_offset,
                                    VP8_BTI_MBENC_IND_MV_DATA);

        if (vp8_context->hme_enabled) {
            i965_add_buffer_2d_gpe_surface(ctx,
                                           gpe_context,
                                           &vp8_context->me_4x_mv_data_buffer,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           VP8_BTI_MBENC_MV_DATA_FROM_ME);
        }

        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->reference_frame_mb_count_buffer,
                                    0,
                                    32, /* sizeof(unsigned int) * 8 */
                                    0,
                                    VP8_BTI_MBENC_REF_MB_COUNT);

        i965_add_adv_gpe_surface(ctx,
                                 gpe_context,
                                 encode_state->input_yuv_object,
                                 VP8_BTI_MBENC_INTER_PRED);

        if (vp8_context->ref_last_frame &&
            vp8_context->ref_last_frame->bo) {
            obj_surface = vp8_context->ref_last_frame;

            switch (vp8_context->ref_frame_ctrl) {
            case 1:
            case 3:
            case 5:
            case 7:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF1_PIC);
                break;
            }
        }

        if (vp8_context->ref_gf_frame &&
            vp8_context->ref_gf_frame->bo) {
            obj_surface = vp8_context->ref_gf_frame;

            switch (vp8_context->ref_frame_ctrl) {
            case 2:
            case 6:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF1_PIC);
                break;

            case 3:
            case 7:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF2_PIC);
                break;
            }
        }

        if (vp8_context->ref_arf_frame &&
            vp8_context->ref_arf_frame->bo) {
            obj_surface = vp8_context->ref_arf_frame;

            switch (vp8_context->ref_frame_ctrl) {
            case 4:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF1_PIC);
                break;

            case 5:
            case 6:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF2_PIC);
                break;

            case 7:
                i965_add_adv_gpe_surface(ctx,
                                         gpe_context,
                                         obj_surface,
                                         VP8_BTI_MBENC_REF3_PIC);
                break;
            }
        }

        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       &vp8_context->per_mb_quant_data_buffer,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_MBENC_P_PER_MB_QUANT);

        i965_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       &vp8_context->me_4x_distortion_buffer,
                                       0,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       VP8_BTI_MBENC_INTER_PRED_DISTORTION);

        size = vp8_context->frame_width_in_mbs * vp8_context->frame_height_in_mbs * 16;
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->pred_mv_data_buffer,
                                    0,
                                    size,
                                    0,
                                    VP8_BTI_MBENC_PRED_MV_DATA);

        size = 16 * sizeof(unsigned int);
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->mode_cost_update_buffer,
                                    1,
                                    size,
                                    0,
                                    VP8_BTI_MBENC_MODE_COST_UPDATE);
    }
}

static void
i965_encoder_vp8_vme_mbenc_init_constant_buffer(VADriverContextP ctx,
                                                struct encode_state *encode_state,
                                                struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    char *pbuffer = NULL;

    i965_zero_gpe_resource(&vp8_context->mb_mode_cost_luma_buffer);
    i965_zero_gpe_resource(&vp8_context->block_mode_cost_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->mb_mode_cost_luma_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, mb_mode_cost_luma_vp8, sizeof(mb_mode_cost_luma_vp8));
    i965_unmap_gpe_resource(&vp8_context->mb_mode_cost_luma_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->block_mode_cost_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, block_mode_cost_vp8, sizeof(block_mode_cost_vp8));
    i965_unmap_gpe_resource(&vp8_context->block_mode_cost_buffer);
}

static VAStatus
i965_encoder_vp8_vme_mbenc(VADriverContextP ctx,
                           struct encode_state *encode_state,
                           struct intel_encoder_context *encoder_context,
                           int is_phase2,
                           int is_iframe_dist)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_mbenc_context *mbenc_context = &vp8_context->mbenc_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context;
    struct mbenc_surface_parameters mbenc_surface_params;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct vp8_encoder_kernel_walker_parameter kernel_walker_param;
    int is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);
    int gpe_index, media_function;

    if (is_iframe_dist) {
        gpe_index = VP8_MBENC_I_FRAME_DIST;
        media_function = VP8_MEDIA_STATE_ENC_I_FRAME_DIST;
    } else if (!is_phase2) {
        if (is_intra) {
            gpe_index = VP8_MBENC_I_FRAME_LUMA;
            media_function = VP8_MEDIA_STATE_ENC_I_FRAME_LUMA;
        } else {
            gpe_index = VP8_MBENC_P_FRAME;
            media_function = VP8_MEDIA_STATE_ENC_P_FRAME;
        }
    } else {
        gpe_index = VP8_MBENC_I_FRAME_CHROMA;
        media_function = VP8_MEDIA_STATE_ENC_I_FRAME_CHROMA;
    }

    gpe_context = &mbenc_context->gpe_contexts[gpe_index];

    if (!is_phase2 || (is_phase2 && vp8_context->brc_mbenc_phase1_ignored)) {
        if (!vp8_context->mbenc_curbe_updated_in_brc_update || is_iframe_dist) {
            VAEncPictureParameterBufferVP8 *pic_param =
                (VAEncPictureParameterBufferVP8 *) encode_state->pic_param_ext->buffer;
            unsigned int ref_frame_flag_final, ref_frame_flag;

            if (!vp8_context->ref_ctrl_optimization_done) {
                if (!is_intra) {
                    ref_frame_flag = VP8_REF_FLAG_ALL;

                    if (pic_param->ref_last_frame == pic_param->ref_gf_frame) {
                        ref_frame_flag &= ~VP8_REF_FLAG_GOLDEN;
                    }

                    if (pic_param->ref_last_frame == pic_param->ref_arf_frame) {
                        ref_frame_flag &= ~VP8_REF_FLAG_ALT;
                    }

                    if (pic_param->ref_gf_frame == pic_param->ref_arf_frame) {
                        ref_frame_flag &= ~VP8_REF_FLAG_ALT;
                    }
                } else {
                    ref_frame_flag = VP8_REF_FLAG_LAST;
                }

                switch (vp8_context->ref_frame_ctrl) {
                case 0:
                    ref_frame_flag_final = VP8_REF_FLAG_NONE;
                    break;

                case 1:
                    ref_frame_flag_final = VP8_REF_FLAG_LAST;
                    break;

                case 2:
                    ref_frame_flag_final = VP8_REF_FLAG_GOLDEN;
                    break;

                case 4:
                    ref_frame_flag_final = VP8_REF_FLAG_ALT;
                    break;

                default:
                    ref_frame_flag_final = ref_frame_flag;
                }

                vp8_context->ref_frame_ctrl = ref_frame_flag_final;
            }

            i965_encoder_vp8_vme_mbenc_set_curbe(ctx, encode_state, encoder_context, gpe_context);
        }

        if (is_intra) {
            i965_encoder_vp8_vme_mbenc_init_constant_buffer(ctx, encode_state, encoder_context);
        }

        if (vp8_context->brc_distortion_buffer_need_reset && is_iframe_dist) {
            i965_encoder_vp8_vme_init_brc_distorion_buffer(ctx, encoder_context);
        }
    }

    if (!is_phase2 || (is_phase2 && vp8_context->brc_mbenc_phase1_ignored)) {
        i965_zero_gpe_resource(&vp8_context->histogram_buffer);
    }

    gpe->reset_binding_table(ctx, gpe_context);

    memset(&mbenc_surface_params, 0, sizeof(mbenc_surface_params));
    mbenc_surface_params.i_frame_dist_in_use = is_iframe_dist;

    if (is_iframe_dist)
        mbenc_surface_params.me_brc_distortion_buffer = &vp8_context->brc_distortion_buffer;
    else
        mbenc_surface_params.me_brc_distortion_buffer = &vp8_context->me_4x_distortion_buffer;

    i965_encoder_vp8_vme_mbenc_add_surfaces(ctx, encode_state, encoder_context, gpe_context, &mbenc_surface_params);

    gpe->setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));

    kernel_walker_param.use_scoreboard = vp8_context->use_hw_scoreboard;

    if (is_iframe_dist) {
        kernel_walker_param.resolution_x = vp8_context->down_scaled_width_in_mb4x;
        kernel_walker_param.resolution_y = vp8_context->down_scaled_height_in_mb4x;
    } else {
        kernel_walker_param.resolution_x = vp8_context->frame_width_in_mbs;
        kernel_walker_param.resolution_y = vp8_context->frame_height_in_mbs;
    }

    if (is_intra && !is_phase2)
        kernel_walker_param.no_dependency = 1;
    else
        kernel_walker_param.walker_degree = VP8_ENCODER_45_DEGREE;

    i965_init_media_object_walker_parameters(encoder_context, &kernel_walker_param, &media_object_walker_param);

    i965_run_kernel_media_object_walker(ctx, encoder_context, gpe_context, media_function, &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_brc_update_set_curbe(VADriverContextP ctx,
                                          struct encode_state *encode_state,
                                          struct intel_encoder_context *encoder_context,
                                          struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_brc_update_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    int is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw2.picture_header_size = 0;

    pcmd->dw3.start_global_adjust_frame0 = 10;
    pcmd->dw3.start_global_adjust_frame1 = 50;

    pcmd->dw4.start_global_adjust_frame2 = 100;
    pcmd->dw4.start_global_adjust_frame3 = 150;

    pcmd->dw5.target_size_flag = 0;

    if (vp8_context->brc_init_current_target_buf_full_in_bits > (double)vp8_context->brc_init_reset_buf_size_in_bits) {
        vp8_context->brc_init_current_target_buf_full_in_bits -= (double)vp8_context->brc_init_reset_buf_size_in_bits;
        pcmd->dw5.target_size_flag = 1;
    }

    pcmd->dw0.target_size = (unsigned int)vp8_context->brc_init_current_target_buf_full_in_bits;

    pcmd->dw5.curr_frame_type = is_intra ? 2 : 0;
    pcmd->dw5.brc_flag = 16 * vp8_context->internal_rate_mode;
    pcmd->dw5.max_num_paks = vp8_context->num_brc_pak_passes;

    pcmd->dw6.tid = 0;
    pcmd->dw6.num_t_levels = 1;

    pcmd->dw8.start_global_adjust_mult0 = 1;
    pcmd->dw8.start_global_adjust_mult1 = 1;
    pcmd->dw8.start_global_adjust_mult2 = 3;
    pcmd->dw8.start_global_adjust_mult3 = 2;

    pcmd->dw9.start_global_adjust_div0 = 40;
    pcmd->dw9.start_global_adjust_div1 = 5;
    pcmd->dw9.start_global_adjust_div2 = 5;
    pcmd->dw9.start_global_adjust_mult4 = 1;

    pcmd->dw10.start_global_adjust_div3 = 3;
    pcmd->dw10.start_global_adjust_div4 = 1;
    pcmd->dw10.qp_threshold0 = 20;
    pcmd->dw10.qp_threshold1 = 40;

    pcmd->dw11.qp_threshold2 = 60;
    pcmd->dw11.qp_threshold3 = 90;
    pcmd->dw11.g_rate_ratio_threshold0 = 40;
    pcmd->dw11.g_rate_ratio_threshold1 = 75;

    pcmd->dw12.g_rate_ratio_threshold2 = 97;
    pcmd->dw12.g_rate_ratio_threshold3 = 103;
    pcmd->dw12.g_rate_ratio_threshold4 = 125;
    pcmd->dw12.g_rate_ratio_threshold5 = 160;

    pcmd->dw13.g_rate_ratio_threshold_qp0 = -3;
    pcmd->dw13.g_rate_ratio_threshold_qp1 = -2;
    pcmd->dw13.g_rate_ratio_threshold_qp2 = -1;
    pcmd->dw13.g_rate_ratio_threshold_qp3 = 0;

    pcmd->dw14.g_rate_ratio_threshold_qp4 = 1;
    pcmd->dw14.g_rate_ratio_threshold_qp5 = 2;
    pcmd->dw14.g_rate_ratio_threshold_qp6 = 3;
    pcmd->dw14.index_of_previous_qp = 0;

    pcmd->dw15.frame_width_in_mb = vp8_context->frame_width_in_mbs;
    pcmd->dw15.frame_height_in_mb = vp8_context->frame_height_in_mbs;

    pcmd->dw16.p_frame_qp_seg0 = quant_param->quantization_index[0];
    pcmd->dw16.p_frame_qp_seg1 = quant_param->quantization_index[1];
    pcmd->dw16.p_frame_qp_seg2 = quant_param->quantization_index[2];
    pcmd->dw16.p_frame_qp_seg3 = quant_param->quantization_index[3];

    pcmd->dw17.key_frame_qp_seg0 = quant_param->quantization_index[0];
    pcmd->dw17.key_frame_qp_seg1 = quant_param->quantization_index[1];
    pcmd->dw17.key_frame_qp_seg2 = quant_param->quantization_index[2];
    pcmd->dw17.key_frame_qp_seg3 = quant_param->quantization_index[3];

    pcmd->dw18.qdelta_plane0 = 0;
    pcmd->dw18.qdelta_plane1 = 0;
    pcmd->dw18.qdelta_plane2 = 0;
    pcmd->dw18.qdelta_plane3 = 0;

    pcmd->dw19.qdelta_plane4 = 0;
    pcmd->dw19.main_ref = is_intra ? 0 : mainref_table_vp8[vp8_context->ref_frame_ctrl];
    pcmd->dw19.ref_frame_flags = is_intra ? 0 : vp8_context->ref_frame_ctrl;

    pcmd->dw20.seg_on = pic_param->pic_flags.bits.segmentation_enabled;
    pcmd->dw20.brc_method = vp8_context->internal_rate_mode;
    pcmd->dw20.mb_rc = 0;

    pcmd->dw20.vme_intra_prediction = (encoder_context->quality_level == ENCODER_LOW_QUALITY) ? 1 : 0;

    pcmd->dw22.historyt_buffer_bti = VP8_BTI_BRC_UPDATE_HISTORY;
    pcmd->dw23.pak_statistics_bti = VP8_BTI_BRC_UPDATE_PAK_STATISTICS_OUTPUT;
    pcmd->dw24.mfx_vp8_encoder_cfg_read_bti = VP8_BTI_BRC_UPDATE_MFX_ENCODER_CFG_READ;
    pcmd->dw25.mfx_vp8_encoder_cfg_write_bti = VP8_BTI_BRC_UPDATE_MFX_ENCODER_CFG_WRITE;
    pcmd->dw26.mbenc_curbe_read_bti = VP8_BTI_BRC_UPDATE_MBENC_CURBE_READ;
    pcmd->dw27.mbenc_curbe_write_bti = VP8_BTI_BRC_UPDATE_MBENC_CURBE_WRITE;
    pcmd->dw28.distortion_bti = VP8_BTI_BRC_UPDATE_DISTORTION_SURFACE;
    pcmd->dw29.constant_data_bti = VP8_BTI_BRC_UPDATE_CONSTANT_DATA;
    pcmd->dw30.segment_map_bti = VP8_BTI_BRC_UPDATE_SEGMENT_MAP;
    pcmd->dw31.mpu_curbe_read_bti = VP8_BTI_BRC_UPDATE_MPU_CURBE_READ;
    pcmd->dw32.mpu_curbe_write_bti = VP8_BTI_BRC_UPDATE_MPU_CURBE_WRITE;
    pcmd->dw33.tpu_curbe_read_bti = VP8_BTI_BRC_UPDATE_TPU_CURBE_READ;
    pcmd->dw34.tpu_curbe_write_bti = VP8_BTI_BRC_UPDATE_TPU_CURBE_WRITE;

    vp8_context->brc_init_current_target_buf_full_in_bits += vp8_context->brc_init_reset_input_bits_per_frame;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_mpu_set_curbe(VADriverContextP ctx,
                                   struct encode_state *encode_state,
                                   struct intel_encoder_context *encoder_context,
                                   struct i965_gpe_context *gpe_context);
static void
i965_encoder_vp8_pak_tpu_set_curbe(VADriverContextP ctx,
                                   struct encode_state *encode_state,
                                   struct intel_encoder_context *encoder_context,
                                   struct i965_gpe_context *gpe_context);

static void
i965_encoder_vp8_vme_brc_update_add_surfaces(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context,
                                             struct i965_gpe_context *gpe_context,
                                             struct brc_update_surface_parameters *params)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    unsigned int size;
    int is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);

    /* BRC history buffer */
    size = VP8_BRC_HISTORY_BUFFER_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_history_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_BRC_UPDATE_HISTORY);

    /* PAK Statistics buffer */
    size = sizeof(struct vp8_brc_pak_statistics);
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_pak_statistics_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_BRC_UPDATE_PAK_STATISTICS_OUTPUT);

    /* Encoder CFG command surface - read only */
    size = VP8_BRC_IMG_STATE_SIZE_PER_PASS * VP8_BRC_MAXIMUM_NUM_PASSES;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_vp8_cfg_command_write_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_BRC_UPDATE_MFX_ENCODER_CFG_READ);

    /* Encoder CFG command surface - write only */
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_vp8_cfg_command_write_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_BRC_UPDATE_MFX_ENCODER_CFG_WRITE);

    /* MBEnc CURBE Buffer - read only */
    size = ALIGN(params->mbenc_gpe_context->curbe.length, 64);
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->mbenc_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->mbenc_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_MBENC_CURBE_READ);

    /* MBEnc CURBE Buffer - write only */
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->mbenc_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->mbenc_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_MBENC_CURBE_WRITE);

    /* BRC Distortion data buffer - input/output */
    i965_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   is_intra ? &vp8_context->brc_distortion_buffer : &vp8_context->me_4x_distortion_buffer,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   VP8_BTI_BRC_UPDATE_DISTORTION_SURFACE);

    /* Constant Data Surface */
    size = VP8_BRC_CONSTANT_DATA_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->brc_vp8_constant_data_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_BRC_UPDATE_CONSTANT_DATA);

    /* Segmap surface */
    i965_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &vp8_context->brc_segment_map_buffer,
                                   0,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   VP8_BTI_BRC_UPDATE_SEGMENT_MAP);

    /* MPU CURBE Buffer - read only */
    size = ALIGN(params->mpu_gpe_context->curbe.length, 64);
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->mpu_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->mpu_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_MPU_CURBE_READ);

    /* MPU CURBE Buffer - write only */
    size = ALIGN(params->mpu_gpe_context->curbe.length, 64);
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->mpu_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->mpu_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_MPU_CURBE_WRITE);

    /* TPU CURBE Buffer - read only */
    size = ALIGN(params->tpu_gpe_context->curbe.length, 64);
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->tpu_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->tpu_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_TPU_CURBE_READ);

    /* TPU CURBE Buffer - write only */
    size = ALIGN(params->tpu_gpe_context->curbe.length, 64);
    i965_add_dri_buffer_gpe_surface(ctx,
                                    encoder_context,
                                    gpe_context,
                                    params->tpu_gpe_context->curbe.bo,
                                    0,
                                    size,
                                    params->tpu_gpe_context->curbe.offset,
                                    VP8_BTI_BRC_UPDATE_TPU_CURBE_WRITE);
}

static void
i965_encoder_vp8_vme_init_brc_update_constant_data(VADriverContextP ctx,
                                                   struct encode_state *encode_state,
                                                   struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    char *pbuffer;

    pbuffer = i965_map_gpe_resource(&vp8_context->brc_vp8_constant_data_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer,
           brc_qpadjustment_distthreshold_maxframethreshold_distqpadjustment_ipb_vp8,
           sizeof(brc_qpadjustment_distthreshold_maxframethreshold_distqpadjustment_ipb_vp8));
    pbuffer += sizeof(brc_qpadjustment_distthreshold_maxframethreshold_distqpadjustment_ipb_vp8);

    memcpy(pbuffer, brc_iframe_cost_vp8, sizeof(brc_iframe_cost_vp8));
    pbuffer += sizeof(brc_iframe_cost_vp8);

    memcpy(pbuffer, brc_pframe_cost_vp8, sizeof(brc_pframe_cost_vp8));
    pbuffer += sizeof(brc_pframe_cost_vp8);

    memcpy(pbuffer, quant_dc_vp8, sizeof(quant_dc_vp8));
    pbuffer += sizeof(quant_dc_vp8);

    memcpy(pbuffer, quant_ac_vp8, sizeof(quant_ac_vp8));
    pbuffer += sizeof(quant_ac_vp8);

    memcpy(pbuffer, brc_skip_mv_threshold_vp8, sizeof(brc_skip_mv_threshold_vp8));

    i965_unmap_gpe_resource(&vp8_context->brc_vp8_constant_data_buffer);
}

static void
i965_encoder_vp8_vme_init_mfx_config_command(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context,
                                             struct vp8_mpu_encoder_config_parameters *params);

static VAStatus
i965_encoder_vp8_vme_brc_update(VADriverContextP ctx,
                                struct encode_state *encode_state,
                                struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_brc_update_context *brc_update_context = &vp8_context->brc_update_context;
    struct i965_encoder_vp8_mbenc_context *mbenc_context = &vp8_context->mbenc_context;
    struct i965_encoder_vp8_mpu_context *mpu_context = &vp8_context->mpu_context;
    struct i965_encoder_vp8_tpu_context *tpu_context = &vp8_context->tpu_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context, *mbenc_gpe_context, *mpu_gpe_context, *tpu_gpe_context;
    struct brc_update_surface_parameters brc_update_surface_params;
    struct gpe_media_object_parameter media_object_param;
    struct vp8_mpu_encoder_config_parameters config_params;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    unsigned int ref_frame_flag_final, ref_frame_flag;
    int is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);
    int media_function = VP8_MEDIA_STATE_BRC_UPDATE;
    int i;

    gpe_context = &brc_update_context->gpe_contexts[0];

    if (is_intra)
        mbenc_gpe_context = &mbenc_context->gpe_contexts[VP8_MBENC_I_FRAME_LUMA];
    else
        mbenc_gpe_context = &mbenc_context->gpe_contexts[VP8_MBENC_P_FRAME];

    mpu_gpe_context = &mpu_context->gpe_contexts[0];
    tpu_gpe_context = &tpu_context->gpe_contexts[0];

    if (!is_intra) {
        ref_frame_flag = VP8_REF_FLAG_ALL;

        if (pic_param->ref_last_frame == pic_param->ref_gf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_GOLDEN;
        }

        if (pic_param->ref_last_frame == pic_param->ref_arf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_ALT;
        }

        if (pic_param->ref_gf_frame == pic_param->ref_arf_frame) {
            ref_frame_flag &= ~VP8_REF_FLAG_ALT;
        }
    } else {
        ref_frame_flag = VP8_REF_FLAG_LAST;
    }

    switch (vp8_context->ref_frame_ctrl) {
    case 0:
        ref_frame_flag_final = VP8_REF_FLAG_NONE;
        break;

    case 1:
        ref_frame_flag_final = VP8_REF_FLAG_LAST;       // Last Ref only
        break;

    case 2:
        ref_frame_flag_final = VP8_REF_FLAG_GOLDEN;     // Gold Ref only
        break;

    case 4:
        ref_frame_flag_final = VP8_REF_FLAG_ALT;        // Alt Ref only
        break;

    default:
        ref_frame_flag_final = ref_frame_flag;
    }

    vp8_context->ref_frame_ctrl = ref_frame_flag_final;
    i965_encoder_vp8_vme_mbenc_set_curbe(ctx, encode_state, encoder_context, mbenc_gpe_context);
    vp8_context->mbenc_curbe_updated_in_brc_update = 1;

    /* Set MPU & TPU curbe here */
    i965_encoder_vp8_vme_mpu_set_curbe(ctx, encode_state, encoder_context, mpu_gpe_context);
    vp8_context->mpu_curbe_updated_in_brc_update = 1;

    i965_encoder_vp8_pak_tpu_set_curbe(ctx, encode_state, encoder_context, tpu_gpe_context);
    vp8_context->tpu_curbe_updated_in_brc_update = 1;

    gpe->context_init(ctx, gpe_context);
    gpe->reset_binding_table(ctx, gpe_context);

    i965_encoder_vp8_vme_brc_update_set_curbe(ctx, encode_state, encoder_context, gpe_context);

    if (vp8_context->brc_constant_buffer_supported) {
        i965_encoder_vp8_vme_init_brc_update_constant_data(ctx, encode_state, encoder_context);
    }

    memset(&config_params, 0, sizeof(config_params));
    config_params.buffer_size = VP8_HEADER_METADATA_SIZE;
    config_params.config_buffer = &vp8_context->brc_vp8_cfg_command_write_buffer;

    for (i = 0; i < VP8_BRC_MAXIMUM_NUM_PASSES; i++) {
        config_params.is_first_pass = !i;
        config_params.command_offset = i * VP8_HEADER_METADATA_SIZE;
        i965_encoder_vp8_vme_init_mfx_config_command(ctx, encode_state, encoder_context, &config_params);
    }

    vp8_context->mfx_encoder_config_command_initialized = 1;

    memset(&brc_update_surface_params, 0, sizeof(brc_update_surface_params));
    brc_update_surface_params.mbenc_gpe_context = mbenc_gpe_context;
    brc_update_surface_params.mpu_gpe_context = mpu_gpe_context;
    brc_update_surface_params.tpu_gpe_context = tpu_gpe_context;
    i965_encoder_vp8_vme_brc_update_add_surfaces(ctx, encode_state, encoder_context, gpe_context, &brc_update_surface_params);

    gpe->setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    i965_run_kernel_media_object(ctx, encoder_context, gpe_context, media_function, &media_object_param);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_mpu_set_curbe(VADriverContextP ctx,
                                   struct encode_state *encode_state,
                                   struct intel_encoder_context *encoder_context,
                                   struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_mpu_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    VAEncSequenceParameterBufferVP8 *seq_param = (VAEncSequenceParameterBufferVP8 *)encode_state->seq_param_ext->buffer;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw0.frame_width = vp8_context->frame_width;
    pcmd->dw0.frame_height = vp8_context->frame_height;

    pcmd->dw1.frame_type = pic_param->pic_flags.bits.frame_type;
    pcmd->dw1.version = pic_param->pic_flags.bits.version;
    pcmd->dw1.show_frame = pic_param->pic_flags.bits.show_frame;
    pcmd->dw1.horizontal_scale_code = seq_param->frame_width_scale;
    pcmd->dw1.vertical_scale_code = seq_param->frame_height_scale;
    pcmd->dw1.color_space_type = pic_param->pic_flags.bits.color_space;
    pcmd->dw1.clamp_type = pic_param->pic_flags.bits.clamping_type;
    pcmd->dw1.partition_num_l2 = pic_param->pic_flags.bits.num_token_partitions;
    pcmd->dw1.enable_segmentation = pic_param->pic_flags.bits.segmentation_enabled;
    pcmd->dw1.seg_map_update = pic_param->pic_flags.bits.segmentation_enabled ? pic_param->pic_flags.bits.update_mb_segmentation_map : 0;
    pcmd->dw1.segmentation_feature_update = pic_param->pic_flags.bits.update_segment_feature_data;
    pcmd->dw1.segmentation_feature_mode = 1;
    pcmd->dw1.loop_filter_type = pic_param->pic_flags.bits.loop_filter_type;
    pcmd->dw1.sharpness_level = pic_param->sharpness_level;
    pcmd->dw1.loop_filter_adjustment_on = pic_param->pic_flags.bits.loop_filter_adj_enable;
    pcmd->dw1.mb_no_coeffiscient_skip = pic_param->pic_flags.bits.mb_no_coeff_skip;
    pcmd->dw1.golden_reference_copy_flag = ((pic_param->pic_flags.bits.refresh_golden_frame == 1) ? 3 : pic_param->pic_flags.bits.copy_buffer_to_golden);
    pcmd->dw1.alternate_reference_copy_flag = ((pic_param->pic_flags.bits.refresh_alternate_frame == 1) ? 3 : pic_param->pic_flags.bits.copy_buffer_to_alternate);
    pcmd->dw1.last_frame_update = pic_param->pic_flags.bits.refresh_last;
    pcmd->dw1.sign_bias_golden = pic_param->pic_flags.bits.sign_bias_golden;
    pcmd->dw1.sign_bias_alt_ref = pic_param->pic_flags.bits.sign_bias_alternate;
    pcmd->dw1.refresh_entropy_p = pic_param->pic_flags.bits.refresh_entropy_probs;

    pcmd->dw2.loop_filter_level = pic_param->loop_filter_level[0];
    pcmd->dw2.qindex = quant_param->quantization_index[0];
    pcmd->dw2.y1_dc_qindex = quant_param->quantization_index_delta[0];
    pcmd->dw2.y2_dc_qindex = quant_param->quantization_index_delta[3];

    pcmd->dw3.y2_ac_qindex = quant_param->quantization_index_delta[4];
    pcmd->dw3.uv_dc_qindex = quant_param->quantization_index_delta[1];
    pcmd->dw3.uv_ac_qindex = quant_param->quantization_index_delta[2];
    pcmd->dw3.feature_data0_segment0 = quant_param->quantization_index[0];

    pcmd->dw4.feature_data0_segment1 = quant_param->quantization_index[1];
    pcmd->dw4.feature_data0_segment2 = quant_param->quantization_index[2];
    pcmd->dw4.feature_data0_segment3 = quant_param->quantization_index[3];
    pcmd->dw4.feature_data1_segment0 = pic_param->loop_filter_level[0];

    pcmd->dw5.feature_data1_segment1 = pic_param->loop_filter_level[1];
    pcmd->dw5.feature_data1_segment2 = pic_param->loop_filter_level[2];
    pcmd->dw5.feature_data1_segment3 = pic_param->loop_filter_level[3];
    pcmd->dw5.ref_lf_delta0 = pic_param->ref_lf_delta[0];

    pcmd->dw6.ref_lf_delta1 = pic_param->ref_lf_delta[1];
    pcmd->dw6.ref_lf_delta2 = pic_param->ref_lf_delta[2];
    pcmd->dw6.ref_lf_delta3 = pic_param->ref_lf_delta[3];
    pcmd->dw6.mode_lf_delta0 = pic_param->mode_lf_delta[0];

    pcmd->dw7.mode_lf_delta1 = pic_param->mode_lf_delta[1];
    pcmd->dw7.mode_lf_delta2 = pic_param->mode_lf_delta[2];
    pcmd->dw7.mode_lf_delta3 = pic_param->mode_lf_delta[3];
    pcmd->dw7.mc_filter_select = pic_param->pic_flags.bits.version > 0 ? 1 : 0;
    pcmd->dw7.chroma_full_pixel_mc_filter_mode = pic_param->pic_flags.bits.version < 3 ? 0 : 1;
    pcmd->dw7.max_num_pak_passes = vp8_context->num_brc_pak_passes;
    pcmd->dw7.forced_token_surface_read = 1;
    pcmd->dw7.mode_cost_enable_flag = 1;

    pcmd->dw8.num_t_levels = 1;
    pcmd->dw8.temporal_layer_id = 0;

    pcmd->dw12.histogram_bti = VP8_BTI_MPU_HISTOGRAM;
    pcmd->dw13.reference_mode_probability_bti = VP8_BTI_MPU_REF_MODE_PROBABILITY;
    pcmd->dw14.mode_probability_bti = VP8_BTI_MPU_CURR_MODE_PROBABILITY;
    pcmd->dw15.reference_token_probability_bti = VP8_BTI_MPU_REF_TOKEN_PROBABILITY;
    pcmd->dw16.token_probability_bti = VP8_BTI_MPU_CURR_TOKEN_PROBABILITY;
    pcmd->dw17.frame_header_bitstream_bti = VP8_BTI_MPU_HEADER_BITSTREAM;
    pcmd->dw18.header_meta_data_bti = VP8_BTI_MPU_HEADER_METADATA;
    pcmd->dw19.picture_state_bti = VP8_BTI_MPU_PICTURE_STATE;
    pcmd->dw20.mpu_bitstream_bti = VP8_BTI_MPU_MPU_BITSTREAM;
    pcmd->dw21.token_bits_data_bti = VP8_BTI_MPU_TOKEN_BITS_DATA_TABLE;
    pcmd->dw22.kernel_debug_dump_bti = VP8_BTI_MPU_VME_DEBUG_STREAMOUT;
    pcmd->dw23.entropy_cost_bti = VP8_BTI_MPU_ENTROPY_COST_TABLE;
    pcmd->dw24.mode_cost_update_bti = VP8_BTI_MPU_MODE_COST_UPDATE;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_vme_mpu_add_surfaces(VADriverContextP ctx,
                                      struct encode_state *encode_state,
                                      struct intel_encoder_context *encoder_context,
                                      struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    unsigned int size;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    /* Histogram buffer */
    size = VP8_HISTOGRAM_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->histogram_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_HISTOGRAM);

    // Reference mode probability
    size = VP8_MODE_PROPABILITIES_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_ref_mode_probs_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_REF_MODE_PROBABILITY);

    // Mode probability
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_mode_probs_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_CURR_MODE_PROBABILITY);

    // Reference Token probability
    size = VP8_COEFFS_PROPABILITIES_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_REF_TOKEN_PROBABILITY);

    // Token probability
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_coeff_probs_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_CURR_TOKEN_PROBABILITY);

    // Frame header
    size = VP8_FRAME_HEADER_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_frame_header_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_MPU_HEADER_BITSTREAM);

    // Header Metadata
    size = VP8_HEADER_METADATA_SIZE;

    if (brc_enabled) {
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->brc_vp8_cfg_command_write_buffer,
                                    0,
                                    size,
                                    0,
                                    VP8_BTI_MPU_HEADER_METADATA);
    } else {
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->pak_mpu_tpu_picture_state_buffer,
                                    0,
                                    size,
                                    VP8_HEADER_METADATA_OFFSET,
                                    VP8_BTI_MPU_HEADER_METADATA);
    }

    // Picture state MFX_VP8_PIC_STATE
    size = 38 * sizeof(unsigned int);
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_picture_state_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_MPU_PICTURE_STATE);

    // Mpu Bitstream
    size = VP8_MPU_BITSTREAM_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_mpu_bitstream_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_MPU_MPU_BITSTREAM);

    // Token bits Data Surface
    size = VP8_TOKEN_BITS_DATA_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_token_bits_data_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_TOKEN_BITS_DATA_TABLE);

    // Entropy cost table
    size = VP8_ENTROPY_COST_TABLE_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_entropy_cost_table_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_MPU_ENTROPY_COST_TABLE);

    //Mode Cost Update Surface
    size = 16 * sizeof(unsigned int);
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->mode_cost_update_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_MPU_MODE_COST_UPDATE);
}

static void
i965_encoder_vp8_vme_update_key_frame_mpu_tpu_buffer(VADriverContextP ctx,
                                                     struct encode_state *encode_state,
                                                     struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    char *key_buffer, *pbuffer;

    key_buffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);

    if (!key_buffer)
        return;

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_coeff_probs_buffer);

    if (!pbuffer) {
        i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);

        return;
    }

    memcpy(pbuffer, key_buffer, VP8_COEFFS_PROPABILITIES_SIZE);
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_coeff_probs_buffer);
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_default_coef_probs, sizeof(vp8_default_coef_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_hw_token_probability_pak_pass_2_buffer);

    if (!pbuffer)
        return;

    memcpy(pbuffer, vp8_default_coef_probs, sizeof(vp8_default_coef_probs));
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_hw_token_probability_pak_pass_2_buffer);
}

static void
i965_encoder_vp8_vme_init_mfx_config_command(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context,
                                             struct vp8_mpu_encoder_config_parameters *params)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct vp8_mfx_encoder_cfg_cmd *pcmd;
    VAEncSequenceParameterBufferVP8 *seq_param = (VAEncSequenceParameterBufferVP8 *)encode_state->seq_param_ext->buffer;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;
    unsigned int segmentation_enabled = pic_param->pic_flags.bits.segmentation_enabled;
    int i;
    char *pbuffer;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    pbuffer = i965_map_gpe_resource(params->config_buffer);

    if (!pbuffer)
        return;

    pbuffer += params->command_offset;
    memset(pbuffer, 0, params->buffer_size);

    pcmd = (struct vp8_mfx_encoder_cfg_cmd *)pbuffer;

    pcmd->dw0.value = (MFX_VP8_ENCODER_CFG | (sizeof(*pcmd) / 4 - 2));

    pcmd->dw1.rate_control_initial_pass = params->is_first_pass ? 1 : 0;
    pcmd->dw1.per_segment_delta_qindex_loop_filter_disable  = (params->is_first_pass || !brc_enabled);
    pcmd->dw1.token_statistics_output_enable = 1;

    if (segmentation_enabled) {
        for (i = 1; i < 4; i++) {
            if ((quant_param->quantization_index[i] != quant_param->quantization_index[0]) ||
                (pic_param->loop_filter_level[i] != pic_param->loop_filter_level[0])) {
                pcmd->dw1.update_segment_feature_data_flag = 1;
                break;
            }
        }
    }

    if (brc_enabled) {
        pcmd->dw2.max_frame_bit_count_rate_control_enable_mask = 1;
        pcmd->dw2.min_frame_bit_count_rate_control_enable_mask = 1;
    }

    pcmd->dw22.show_frame = pic_param->pic_flags.bits.show_frame;
    pcmd->dw22.bitstream_format_version = pic_param->pic_flags.bits.version;

    pcmd->dw23.horizontal_size_code = ((seq_param->frame_width_scale << 14) | seq_param->frame_width);
    pcmd->dw23.vertical_size_code = ((seq_param->frame_height_scale << 14) | seq_param->frame_height);

    //Add batch buffer end command
    pbuffer += sizeof(*pcmd);
    *((unsigned int *)pbuffer) = MI_BATCH_BUFFER_END;

    i965_unmap_gpe_resource(params->config_buffer);
}

static VAStatus
i965_encoder_vp8_vme_mpu(VADriverContextP ctx,
                         struct encode_state *encode_state,
                         struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_mpu_context *mpu_context = &vp8_context->mpu_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_media_object_parameter media_object_param;
    struct i965_gpe_context *gpe_context;
    struct vp8_mpu_encoder_config_parameters config_params;
    int media_function = VP8_MEDIA_STATE_MPU;

    gpe_context = &mpu_context->gpe_contexts[0];
    /* gpe->context_init(ctx, gpe_context); */
    gpe->reset_binding_table(ctx, gpe_context);

    if (vp8_context->frame_type == MPEG_I_PICTURE)
        i965_encoder_vp8_vme_update_key_frame_mpu_tpu_buffer(ctx, encode_state, encoder_context);

    if (!vp8_context->mfx_encoder_config_command_initialized) {
        memset(&config_params, 0, sizeof(config_params));
        config_params.is_first_pass = !vp8_context->curr_pass;
        config_params.command_offset = VP8_HEADER_METADATA_OFFSET;
        config_params.buffer_size = VP8_PICTURE_STATE_SIZE;
        config_params.config_buffer = &vp8_context->pak_mpu_tpu_picture_state_buffer;
        i965_encoder_vp8_vme_init_mfx_config_command(ctx, encode_state, encoder_context, &config_params);
    }

    if (!vp8_context->mpu_curbe_updated_in_brc_update)
        i965_encoder_vp8_vme_mpu_set_curbe(ctx, encode_state, encoder_context, gpe_context);

    i965_encoder_vp8_vme_mpu_add_surfaces(ctx, encode_state, encoder_context, gpe_context);
    gpe->setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    i965_run_kernel_media_object(ctx, encoder_context, gpe_context, media_function, &media_object_param);

    return VA_STATUS_SUCCESS;
}

static VAStatus
i965_encoder_vp8_vme_gpe_kernel_function(VADriverContextP ctx,
                                         struct encode_state *encode_state,
                                         struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    int is_intra = (vp8_context->frame_type == MPEG_I_PICTURE);
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);
    unsigned char scaling_enabled = vp8_context->hme_supported;
    unsigned char scaling_16x_enabled = vp8_context->hme_16x_supported;
    unsigned char hme_enabled = vp8_context->hme_enabled;
    unsigned char hme_16x_enabled = vp8_context->hme_16x_enabled;

    if (brc_enabled) {
        if (!vp8_context->brc_initted || vp8_context->brc_need_reset) {
            i965_encoder_vp8_vme_brc_init_reset(ctx, encode_state, encoder_context);
        }
    }

    if (scaling_enabled) {
        i965_encoder_vp8_vme_scaling(ctx, encode_state, encoder_context, 0);

        if (scaling_16x_enabled)
            i965_encoder_vp8_vme_scaling(ctx, encode_state, encoder_context, 1);
    }

    if (hme_enabled) {
        if (hme_16x_enabled)
            i965_encoder_vp8_vme_me(ctx, encode_state, encoder_context, 1);

        i965_encoder_vp8_vme_me(ctx, encode_state, encoder_context, 0);
    }

    if (brc_enabled) {
        if (is_intra) {
            i965_encoder_vp8_vme_mbenc(ctx, encode_state, encoder_context, 0, 1);
        }

        i965_encoder_vp8_vme_brc_update(ctx, encode_state, encoder_context);
    }

    vp8_context->brc_initted = 1;
    vp8_context->brc_mbenc_phase1_ignored = 0;

    if (is_intra && encoder_context->quality_level == ENCODER_LOW_QUALITY) {
        vp8_context->brc_mbenc_phase1_ignored = 1;
    } else {
        i965_encoder_vp8_vme_mbenc(ctx, encode_state, encoder_context, 0, 0);
    }

    if (is_intra) {
        i965_encoder_vp8_vme_mbenc(ctx, encode_state, encoder_context, 1, 0);
    }

    i965_encoder_vp8_vme_mpu(ctx, encode_state, encoder_context);

    return VA_STATUS_SUCCESS;
}

static VAStatus
i965_encoder_vp8_vme_gpe_kernel_final(VADriverContextP ctx,
                                      struct encode_state *encode_state,
                                      struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_mbenc_context *mbenc_context = &vp8_context->mbenc_context;
    struct i965_encoder_vp8_mpu_context *mpu_context = &vp8_context->mpu_context;

    dri_bo_unreference(mbenc_context->luma_chroma_dynamic_buffer);
    mbenc_context->luma_chroma_dynamic_buffer = NULL;

    dri_bo_unreference(mpu_context->dynamic_buffer);
    mpu_context->dynamic_buffer = NULL;

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_set_status_buffer(VADriverContextP ctx,
                                       struct encode_state *encode_state,
                                       struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;
    struct i965_encoder_vp8_encode_status_buffer *encode_status_buffer = &vp8_context->encode_status_buffer;
    struct vp8_encode_status *encode_status;
    char *pbuffer;

    dri_bo_unreference(encode_status_buffer->bo);
    encode_status_buffer->bo = encode_state->coded_buf_object->buffer_store->bo;
    dri_bo_reference(encode_status_buffer->bo);

    encode_status_buffer->base_offset = offsetof(struct i965_coded_buffer_segment, codec_private_data);
    encode_status_buffer->size = ALIGN(sizeof(struct vp8_encode_status), sizeof(unsigned int) * 2);

    encode_status_buffer->bitstream_byte_count_offset = offsetof(struct vp8_encode_status, bitstream_byte_count_per_frame);
    encode_status_buffer->image_status_mask_offset = offsetof(struct vp8_encode_status, image_status_mask);
    encode_status_buffer->image_status_ctrl_offset = offsetof(struct vp8_encode_status, image_status_ctrl);

    dri_bo_map(encode_status_buffer->bo, 1);

    if (!encode_status_buffer->bo->virtual)
        return;

    pbuffer = encode_status_buffer->bo->virtual;
    pbuffer += encode_status_buffer->base_offset;
    encode_status = (struct vp8_encode_status *)pbuffer;
    memset(encode_status, 0, sizeof(*encode_status));

    dri_bo_unmap(encode_status_buffer->bo);
}

static VAStatus
i965_encoder_vp8_vme_pipeline(VADriverContextP ctx,
                              VAProfile profile,
                              struct encode_state *encode_state,
                              struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    vp8_context->is_render_context = 1;

    i965_encoder_vp8_vme_set_status_buffer(ctx, encode_state, encoder_context);

    i965_encoder_vp8_get_paramters(ctx, encode_state, encoder_context);

    i965_encoder_vp8_vme_gpe_kernel_init(ctx, encode_state, encoder_context);
    i965_encoder_vp8_vme_gpe_kernel_function(ctx, encode_state, encoder_context);
    i965_encoder_vp8_vme_gpe_kernel_final(ctx, encode_state, encoder_context);

    vp8_context->frame_num++;
    vp8_context->brc_need_reset = 0;

    vp8_context->mbenc_curbe_updated_in_brc_update = 0;
    vp8_context->mpu_curbe_updated_in_brc_update = 0;
    vp8_context->mfx_encoder_config_command_initialized = 0;

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_vme_kernel_context_destroy(struct i965_encoder_vp8_context *vp8_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    int i;

    for (i = 0; i < NUM_VP8_BRC_RESET; i++)
        gpe->context_destroy(&vp8_context->brc_init_reset_context.gpe_contexts[i]);

    for (i = 0; i < NUM_VP8_SCALING; i++)
        gpe->context_destroy(&vp8_context->scaling_context.gpe_contexts[i]);

    for (i = 0; i < NUM_VP8_ME; i++)
        gpe->context_destroy(&vp8_context->me_context.gpe_contexts[i]);

    for (i = 0; i < NUM_VP8_MBENC; i++)
        gpe->context_destroy(&vp8_context->mbenc_context.gpe_contexts[i]);

    for (i = 0; i < NUM_VP8_BRC_UPDATE; i++)
        gpe->context_destroy(&vp8_context->brc_update_context.gpe_contexts[i]);

    for (i = 0; i < NUM_VP8_MPU; i++)
        gpe->context_destroy(&vp8_context->mpu_context.gpe_contexts[i]);

    i965_encoder_vp8_vme_free_resources(vp8_context);
}

static void
i965_encoder_vp8_vme_context_destroy(void *context)
{
    struct i965_encoder_vp8_context *vp8_context = context;
    struct i965_encoder_vp8_encode_status_buffer *encode_status_buffer = &vp8_context->encode_status_buffer;

    i965_encoder_vp8_vme_kernel_context_destroy(vp8_context);

    dri_bo_unreference(encode_status_buffer->bo);
    encode_status_buffer->bo = NULL;

    free(vp8_context);
}

static void
i965_encoder_vp8_vme_brc_init_reset_context_init(VADriverContextP ctx,
                                                 struct i965_encoder_vp8_context *vp8_context,
                                                 struct i965_encoder_vp8_brc_init_reset_context *brc_init_reset_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_brc_init_reset_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_BRC_RESET; i++) {
        gpe_context = &brc_init_reset_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_brc_init_reset[i],
                          1);
    }
}

static void
i965_encoder_vp8_vme_scaling_context_init(VADriverContextP ctx,
                                          struct i965_encoder_vp8_context *vp8_context,
                                          struct i965_encoder_vp8_scaling_context *scaling_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_scaling_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_SCALING; i++) {
        gpe_context = &scaling_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_scaling[i],
                          1);
    }
}

static void
i965_encoder_vp8_vme_me_context_init(VADriverContextP ctx,
                                     struct i965_encoder_vp8_context *vp8_context,
                                     struct i965_encoder_vp8_me_context *me_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_me_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_ME; i++) {
        gpe_context = &me_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_me[i],
                          1);
    }
}

static void
i965_encoder_vp8_vme_mbenc_context_init(VADriverContextP ctx,
                                        struct i965_encoder_vp8_context *vp8_context,
                                        struct i965_encoder_vp8_mbenc_context *mbenc_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = MAX(sizeof(struct vp8_mbenc_i_frame_curbe_data), sizeof(struct vp8_mbenc_p_frame_curbe_data));
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_MBENC; i++) {
        gpe_context = &mbenc_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_mbenc[i],
                          1);
    }
}

static void
i965_encoder_vp8_vme_brc_update_context_init(VADriverContextP ctx,
                                             struct i965_encoder_vp8_context *vp8_context,
                                             struct i965_encoder_vp8_brc_update_context *brc_update_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_brc_update_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_BRC_UPDATE; i++) {
        gpe_context = &brc_update_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_brc_update[i],
                          1);
    }
}

static void
i965_encoder_vp8_vme_mpu_context_init(VADriverContextP ctx,
                                      struct i965_encoder_vp8_context *vp8_context,
                                      struct i965_encoder_vp8_mpu_context *mpu_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_mpu_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_MPU; i++) {
        gpe_context = &mpu_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_mpu[i],
                          1);
    }
}

static Bool
i965_encoder_vp8_vme_var_init(VADriverContextP ctx,
                              struct intel_encoder_context *encoder_context,
                              struct i965_encoder_vp8_context *vp8_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);

    vp8_context->mocs = i965->intel.mocs_state;

    vp8_context->gpe_table = &i965->gpe_table;

    vp8_context->min_scaled_dimension = 48;
    vp8_context->min_scaled_dimension_in_mbs = WIDTH_IN_MACROBLOCKS(vp8_context->min_scaled_dimension);

    vp8_context->vdbox_idc = BSD_RING0;
    vp8_context->vdbox_mmio_base = VDBOX0_MMIO_BASE;

    /* TODO: This is a WA for VDBOX loading balance only, */
    if (i965->intel.has_bsd2) {
        srandom(time(NULL));
        vp8_context->vdbox_idc = (random() % 2 ? BSD_RING1 : BSD_RING0);
    }

    if (vp8_context->vdbox_idc == BSD_RING1)
        vp8_context->vdbox_mmio_base = VDBOX1_MMIO_BASE;

    vp8_context->frame_type = MPEG_I_PICTURE;

    vp8_context->use_hw_scoreboard = 1;
    vp8_context->use_hw_non_stalling_scoreborad = 1; /* default: non-stalling */
    vp8_context->brc_distortion_buffer_supported = 1;
    vp8_context->brc_constant_buffer_supported = 1;
    vp8_context->repak_supported = 1;
    vp8_context->multiple_pass_brc_supported = 1;
    vp8_context->is_first_frame = 1;
    vp8_context->is_first_two_frame = 1;
    vp8_context->gop_size = 30;
    vp8_context->hme_supported = 1;
    vp8_context->hme_16x_supported = 1;
    vp8_context->hme_enabled = 0;
    vp8_context->hme_16x_enabled = 0;
    vp8_context->brc_initted = 0;
    vp8_context->frame_num = 0;
    vp8_context->framerate = (struct intel_fraction) {
        30, 1
    };

    return True;
}

static Bool
i965_encoder_vp8_vme_kernels_context_init(VADriverContextP ctx,
                                          struct intel_encoder_context *encoder_context,
                                          struct i965_encoder_vp8_context *vp8_context)
{
    i965_encoder_vp8_vme_brc_init_reset_context_init(ctx, vp8_context, &vp8_context->brc_init_reset_context);
    i965_encoder_vp8_vme_scaling_context_init(ctx, vp8_context, &vp8_context->scaling_context);
    i965_encoder_vp8_vme_me_context_init(ctx, vp8_context, &vp8_context->me_context);
    i965_encoder_vp8_vme_mbenc_context_init(ctx, vp8_context, &vp8_context->mbenc_context);
    i965_encoder_vp8_vme_brc_update_context_init(ctx, vp8_context, &vp8_context->brc_update_context);
    i965_encoder_vp8_vme_mpu_context_init(ctx, vp8_context, &vp8_context->mpu_context);

    return True;
}

extern Bool
gen8_encoder_vp8_context_init(VADriverContextP, struct intel_encoder_context *, struct i965_encoder_vp8_context *);

extern Bool
gen9_encoder_vp8_context_init(VADriverContextP, struct intel_encoder_context *, struct i965_encoder_vp8_context *);

extern Bool
gen10_encoder_vp8_context_init(VADriverContextP, struct intel_encoder_context *, struct i965_encoder_vp8_context *);

Bool
i965_encoder_vp8_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct i965_encoder_vp8_context *vp8_context = NULL;

    vp8_context = calloc(1, sizeof(struct i965_encoder_vp8_context));

    if (!vp8_context)
        return False;

    i965_encoder_vp8_vme_var_init(ctx, encoder_context, vp8_context);

    if (IS_CHERRYVIEW(i965->intel.device_info))
        gen8_encoder_vp8_context_init(ctx, encoder_context, vp8_context);
    else if (IS_GEN9(i965->intel.device_info)) {
        gen9_encoder_vp8_context_init(ctx, encoder_context, vp8_context);
    } else if (IS_GEN10(i965->intel.device_info)) {
        gen10_encoder_vp8_context_init(ctx, encoder_context, vp8_context);
    } else {
        free(vp8_context);

        return False;
    }

    i965_encoder_vp8_vme_kernels_context_init(ctx, encoder_context, vp8_context);

    encoder_context->vme_context = vp8_context;
    encoder_context->vme_pipeline = i965_encoder_vp8_vme_pipeline;
    encoder_context->vme_context_destroy = i965_encoder_vp8_vme_context_destroy;

    return True;
}

/*
 * PAK part
 */
static void
i965_encoder_vp8_pak_pre_pipeline(struct encode_state *encode_state,
                                  struct intel_encoder_context *encoder_context)
{
    /* No thing to do */
}

static void
i965_encoder_vp8_pak_kernels_context_destroy(struct i965_encoder_vp8_context *vp8_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    int i;

    for (i = 0; i < NUM_VP8_TPU; i++)
        gpe->context_destroy(&vp8_context->tpu_context.gpe_contexts[i]);
}


static void
i965_encoder_vp8_pak_context_destroy(void *context)
{
    struct i965_encoder_vp8_context *vp8_context = context;
    int i;

    dri_bo_unreference(vp8_context->post_deblocking_output.bo);
    vp8_context->post_deblocking_output.bo = NULL;

    dri_bo_unreference(vp8_context->pre_deblocking_output.bo);
    vp8_context->pre_deblocking_output.bo = NULL;

    dri_bo_unreference(vp8_context->uncompressed_picture_source.bo);
    vp8_context->uncompressed_picture_source.bo = NULL;

    dri_bo_unreference(vp8_context->indirect_pak_bse_object.bo);
    vp8_context->indirect_pak_bse_object.bo = NULL;

    for (i = 0; i < MAX_MFX_REFERENCE_SURFACES; i++) {
        dri_bo_unreference(vp8_context->reference_surfaces[i].bo);
        vp8_context->reference_surfaces[i].bo = NULL;
    }

    i965_encoder_vp8_pak_kernels_context_destroy(vp8_context);

    /* vme & pak same the same structure, so don't free the context here */
}

static void
i965_encoder_vp8_pak_pipe_mode_select(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;

    BEGIN_BCS_BATCH(batch, 5);

    OUT_BCS_BATCH(batch, MFX_PIPE_MODE_SELECT | (5 - 2));
    OUT_BCS_BATCH(batch,
                  (MFX_LONG_MODE << 17) | /* Must be long format for encoder */
                  (MFD_MODE_VLD << 15) |  /* VLD mode */
                  ((!!vp8_context->post_deblocking_output.bo) << 9)  | /* Post Deblocking Output */
                  ((!!vp8_context->pre_deblocking_output.bo) << 8)  |  /* Pre Deblocking Output */
                  (1 << 4)  | /* encoding mode */
                  (MFX_FORMAT_VP8 << 0));
    OUT_BCS_BATCH(batch, 0);
    OUT_BCS_BATCH(batch, 0);
    OUT_BCS_BATCH(batch, 0);

    ADVANCE_BCS_BATCH(batch);
}

static void
i965_encoder_vp8_pak_surface_state(VADriverContextP ctx,
                                   struct object_surface *obj_surface,
                                   int id,
                                   struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;

    BEGIN_BCS_BATCH(batch, 6);

    OUT_BCS_BATCH(batch, MFX_SURFACE_STATE | (6 - 2));
    OUT_BCS_BATCH(batch, id);
    OUT_BCS_BATCH(batch,
                  ((obj_surface->orig_height - 1) << 18) |
                  ((obj_surface->orig_width - 1) << 4));
    OUT_BCS_BATCH(batch,
                  (MFX_SURFACE_PLANAR_420_8 << 28) | /* 420 planar YUV surface */
                  (1 << 27) | /* must be 1 for interleave U/V, hardware requirement */
                  ((obj_surface->width - 1) << 3) |  /* pitch */
                  (0 << 2)  | /* must be 0 for interleave U/V */
                  (1 << 1)  | /* must be tiled */
                  (I965_TILEWALK_YMAJOR << 0));  /* tile walk, TILEWALK_YMAJOR */
    OUT_BCS_BATCH(batch,
                  (0 << 16) |            /* must be 0 for interleave U/V */
                  (obj_surface->height));        /* y offset for U(cb) */
    OUT_BCS_BATCH(batch, 0);

    ADVANCE_BCS_BATCH(batch);
}

#define PAK_OUT_BUFFER_2DW(buf_bo, is_target, delta)  do {              \
        if (buf_bo) {                                                   \
            OUT_BCS_RELOC64(batch,                                      \
                            buf_bo,                                     \
                            I915_GEM_DOMAIN_RENDER,                     \
                            is_target ? I915_GEM_DOMAIN_RENDER : 0,     \
                            delta);                                     \
        } else {                                                        \
            OUT_BCS_BATCH(batch, 0);                                    \
            OUT_BCS_BATCH(batch, 0);                                    \
        }                                                               \
    } while (0)

#define PAK_OUT_BUFFER_3DW(buf_bo, is_target, delta, attr)  do {        \
        PAK_OUT_BUFFER_2DW(buf_bo, is_target, delta);                   \
        OUT_BCS_BATCH(batch, attr);                                     \
    } while (0)



static void
i965_encoder_vp8_pak_pipe_buf_addr_state(VADriverContextP ctx,
                                         struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    int i;

    BEGIN_BCS_BATCH(batch, 61);

    OUT_BCS_BATCH(batch, MFX_PIPE_BUF_ADDR_STATE | (61 - 2));

    /* the DW1-3 is for pre_deblocking */
    PAK_OUT_BUFFER_3DW(vp8_context->pre_deblocking_output.bo, 1, 0, vp8_context->mocs);

    /* the DW4-6 is for the post_deblocking */
    PAK_OUT_BUFFER_3DW(vp8_context->post_deblocking_output.bo, 1, 0, vp8_context->mocs);

    /* the DW7-9 is for the uncompressed_picture */
    PAK_OUT_BUFFER_3DW(vp8_context->uncompressed_picture_source.bo, 0, 0, vp8_context->mocs);

    /* the DW10-12 is for the mb status */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_stream_out_buffer.bo, 1, 0, vp8_context->mocs);

    /* the DW13-15 is for the intra_row_store_scratch */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_intra_row_store_scratch_buffer.bo, 1, 0, vp8_context->mocs);

    /* the DW16-18 is for the deblocking filter */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_deblocking_filter_row_store_scratch_buffer.bo, 1, 0, vp8_context->mocs);

    /* the DW 19-50 is for Reference pictures*/
    for (i = 0; i < ARRAY_ELEMS(vp8_context->reference_surfaces); i++) {
        PAK_OUT_BUFFER_2DW(vp8_context->reference_surfaces[i].bo, 0, 0);
    }

    /* DW 51 */
    OUT_BCS_BATCH(batch, vp8_context->mocs);

    /* The DW 52-54 is for the MB status buffer */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);

    /* the DW 55-57 is the ILDB buffer */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);

    /* the DW 58-60 is the second ILDB buffer */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);

    ADVANCE_BCS_BATCH(batch);
}

static void
i965_encoder_vp8_pak_ind_obj_base_addr_state(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    int vme_size = ALIGN((vp8_context->mb_coded_buffer_size - vp8_context->mv_offset), 0x1000);

    BEGIN_BCS_BATCH(batch, 26);

    OUT_BCS_BATCH(batch, MFX_IND_OBJ_BASE_ADDR_STATE | (26 - 2));

    /* the DW1-5 is for the MFX indirect bistream */
    PAK_OUT_BUFFER_3DW(vp8_context->indirect_pak_bse_object.bo, 1, vp8_context->indirect_pak_bse_object.offset, vp8_context->mocs);
    PAK_OUT_BUFFER_2DW(vp8_context->indirect_pak_bse_object.bo, 1, vp8_context->indirect_pak_bse_object.end_offset);

    /* the DW6-10 is for MFX Indirect MV Object Base Address */
    PAK_OUT_BUFFER_3DW(vp8_context->mb_coded_buffer.bo, 0, vp8_context->mv_offset, vp8_context->mocs);
    PAK_OUT_BUFFER_2DW(vp8_context->mb_coded_buffer.bo, 0, (vp8_context->mv_offset + vme_size));

    /* the DW11-15 is for MFX IT-COFF. Not used on encoder */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);
    PAK_OUT_BUFFER_2DW(NULL, 0, 0);

    /* the DW16-20 is for MFX indirect DBLK. Not used on encoder */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);
    PAK_OUT_BUFFER_2DW(NULL, 0, 0);

    /* the DW21-25 is for MFC Indirect PAK-BSE Object Base Address for Encoder*/
    PAK_OUT_BUFFER_3DW(vp8_context->indirect_pak_bse_object.bo, 1, vp8_context->indirect_pak_bse_object.offset, vp8_context->mocs);
    PAK_OUT_BUFFER_2DW(vp8_context->indirect_pak_bse_object.bo, 1, vp8_context->indirect_pak_bse_object.end_offset);

    ADVANCE_BCS_BATCH(batch);
}

static void
i965_encoder_vp8_pak_bsp_buf_base_addr_state(VADriverContextP ctx,
                                             struct encode_state *encode_state,
                                             struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    int num_partitions = (1 << pic_param->pic_flags.bits.num_token_partitions);
    int offset;
    unsigned int token_size = vp8_context->frame_width * vp8_context->frame_height * 2;
    unsigned int part_size = token_size / num_partitions;
    unsigned int part0_size = (vp8_context->frame_width * vp8_context->frame_height) / 4 + VP8_INTERMEDIATE_PARTITION0_SIZE;

    BEGIN_BCS_BATCH(batch, 32);
    OUT_BCS_BATCH(batch, MFX_VP8_BSP_BUF_BASE_ADDR_STATE | (32 - 2));

    /* The 4th parameter in PAK_OUT_BUFFER_3DW() is not a MOCS index for this command per doc */
    /* DW1-3 */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_frame_header_buffer.bo, 1, 0, 0);
    /* DW4-6 */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_intermediate_buffer.bo, 1, 0, 0);

    /* DW7-DW14 */
    offset = ALIGN(part0_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);
    offset = ALIGN(offset + part_size, 64);
    OUT_BCS_BATCH(batch, offset);

    /* DW15 */
    OUT_BCS_BATCH(batch, token_size + part0_size);

    /* DW16-18 */
    PAK_OUT_BUFFER_3DW(vp8_context->indirect_pak_bse_object.bo, 1, vp8_context->indirect_pak_bse_object.offset, 0);

    /* DW19 */
    OUT_BCS_BATCH(batch, 0);

    /* DW20-22 */
    PAK_OUT_BUFFER_3DW(NULL, 0, 0, 0);

    /* DW23-25 */
    if (vp8_context->repak_pass_iter_val > 0 &&
        vp8_context->frame_type == MPEG_I_PICTURE &&
        vp8_context->repak_pass_iter_val == vp8_context->curr_pass)
        PAK_OUT_BUFFER_3DW(vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer.bo, 1, 0, 0);
    else
        PAK_OUT_BUFFER_3DW(vp8_context->pak_mpu_tpu_coeff_probs_buffer.bo, 1, 0, 0);

    /* DW26-28 */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_mpu_tpu_pak_token_statistics_buffer.bo, 1, 0, 0);

    /* DW29-31 */
    PAK_OUT_BUFFER_3DW(vp8_context->pak_mpc_row_store_scratch_buffer.bo, 1, 0, 0);

    ADVANCE_BCS_BATCH(batch);
}

static void
i965_encoder_vp8_pak_insert_batch_buffers(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_mi_batch_buffer_start_parameter batch_param;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    memset(&batch_param, 0, sizeof(batch_param));
    batch_param.bo = vp8_context->pak_mpu_tpu_picture_state_buffer.bo;
    batch_param.is_second_level = 1; /* Must be the second batch buffer */
    gpe->mi_batch_buffer_start(ctx, batch, &batch_param);

    if (brc_enabled) {
        batch_param.bo = vp8_context->brc_vp8_cfg_command_write_buffer.bo;

        if (vp8_context->repak_pass_iter_val == 0) {
            batch_param.offset = vp8_context->curr_pass * VP8_BRC_IMG_STATE_SIZE_PER_PASS;
        } else {

            if (vp8_context->repak_pass_iter_val == vp8_context->curr_pass)
                batch_param.offset = 0;
            else
                batch_param.offset = vp8_context->curr_pass * VP8_BRC_IMG_STATE_SIZE_PER_PASS;
        }

        gpe->mi_batch_buffer_start(ctx, batch, &batch_param);
    }

    batch_param.bo = vp8_context->mb_coded_buffer.bo;
    batch_param.offset = 0;
    gpe->mi_batch_buffer_start(ctx, batch, &batch_param);
}

static void
i965_encoder_vp8_pak_picture_level(VADriverContextP ctx,
                                   struct encode_state *encode_state,
                                   struct intel_encoder_context *encoder_context)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_encoder_vp8_encode_status_buffer *encode_status_buffer = &vp8_context->encode_status_buffer;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_mi_conditional_batch_buffer_end_parameter mi_param;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    if (brc_enabled &&
        vp8_context->curr_pass > 0 &&
        (vp8_context->curr_pass < vp8_context->repak_pass_iter_val ||
         vp8_context->repak_pass_iter_val == 0)) {
        memset(&mi_param, 0, sizeof(mi_param));
        mi_param.bo = encode_status_buffer->bo;
        mi_param.offset = (encode_status_buffer->base_offset +
                           encode_status_buffer->image_status_mask_offset);
        gpe->mi_conditional_batch_buffer_end(ctx, batch, &mi_param);
    }

    if ((vp8_context->repak_pass_iter_val > 0) && (vp8_context->curr_pass == vp8_context->repak_pass_iter_val)) {
        memset(&mi_param, 0, sizeof(mi_param));
        mi_param.bo = vp8_context->pak_mpu_tpu_repak_decision_buffer.bo;
        mi_param.offset = 0;
        gpe->mi_conditional_batch_buffer_end(ctx, batch, &mi_param);
    }

    i965_encoder_vp8_pak_pipe_mode_select(ctx, encoder_context);
    i965_encoder_vp8_pak_surface_state(ctx, encode_state->reconstructed_object, 0, encoder_context);
    i965_encoder_vp8_pak_surface_state(ctx, encode_state->input_yuv_object, 4, encoder_context);
    i965_encoder_vp8_pak_pipe_buf_addr_state(ctx, encoder_context);
    i965_encoder_vp8_pak_ind_obj_base_addr_state(ctx, encoder_context);
    i965_encoder_vp8_pak_bsp_buf_base_addr_state(ctx, encode_state, encoder_context);
    i965_encoder_vp8_pak_insert_batch_buffers(ctx, encoder_context);
}

static void
i965_encoder_vp8_pak_set_pak_status_in_tpu_curbe(VADriverContextP ctx,
                                                 struct intel_encoder_context *encoder_context,
                                                 int ipass)
{
    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_encoder_vp8_tpu_context *tpu_context = &vp8_context->tpu_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *tpu_gpe_context;
    struct gpe_mi_store_data_imm_parameter mi_store_data_imm_param;
    struct gpe_mi_store_register_mem_parameter mi_store_register_mem_param;

    tpu_gpe_context = &tpu_context->gpe_contexts[0];

    memset(&mi_store_data_imm_param, 0, sizeof(mi_store_data_imm_param));
    mi_store_data_imm_param.bo = tpu_gpe_context->curbe.bo;
    mi_store_data_imm_param.offset = tpu_gpe_context->curbe.offset + sizeof(unsigned int) * 6;
    mi_store_data_imm_param.dw0 = (vp8_context->curr_pass + 1) << 8;

    gpe->mi_store_data_imm(ctx, batch, &mi_store_data_imm_param);

    if (ipass == 0) {
        memset(&mi_store_register_mem_param, 0, sizeof(mi_store_register_mem_param));
        mi_store_register_mem_param.bo = tpu_gpe_context->curbe.bo;
        mi_store_register_mem_param.offset = tpu_gpe_context->curbe.offset + sizeof(unsigned int) * 8;
        mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_DQ_INDEX01_REG_OFFSET;
        gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

        mi_store_register_mem_param.offset = tpu_gpe_context->curbe.offset + sizeof(unsigned int) * 9;
        mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_DQ_INDEX23_REG_OFFSET;
        gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

        mi_store_register_mem_param.offset = tpu_gpe_context->curbe.offset + sizeof(unsigned int) * 10;
        mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_D_LOOP_FILTER01_REG_OFFSET;
        gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);

        mi_store_register_mem_param.offset = tpu_gpe_context->curbe.offset + sizeof(unsigned int) * 11;
        mi_store_register_mem_param.mmio_offset = vp8_context->vdbox_mmio_base + VP8_MFX_BRC_CUMULATIVE_D_LOOP_FILTER23_REG_OFFSET;
        gpe->mi_store_register_mem(ctx, batch, &mi_store_register_mem_param);
    }
}

static void
i965_encoder_vp8_pak_slice_level_brc(VADriverContextP ctx,
                                     struct intel_encoder_context *encoder_context,
                                     int ipass)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    unsigned int *pbuffer;

    i965_encoder_vp8_read_pak_statistics(ctx, encoder_context, ipass);

    pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);

    if (!pbuffer)
        return;

    pbuffer += 38;
    *pbuffer = 0x05000000;
    i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);

    i965_encoder_vp8_pak_set_pak_status_in_tpu_curbe(ctx, encoder_context, ipass);
}

static void
i965_encoder_vp8_pak_slice_level(VADriverContextP ctx,
                                 struct encode_state *encode_state,
                                 struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);
    unsigned int *pbuffer;

    i965_encoder_vp8_read_encode_status(ctx, encoder_context);

    if (vp8_context->num_brc_pak_passes == VP8_BRC_SINGLE_PASS) {
        if (brc_enabled) {
            i965_encoder_vp8_read_pak_statistics(ctx, encoder_context, vp8_context->curr_pass);

            /* Workaround: */
            pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);

            if (!pbuffer)
                return;

            pbuffer += 38;
            *pbuffer = 0x05000000;
            i965_unmap_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);
        }

        vp8_context->submit_batchbuffer = 1;
    } else {
        if ((brc_enabled) &&
            ((vp8_context->curr_pass < vp8_context->num_passes && vp8_context->repak_pass_iter_val > 0) ||
             (vp8_context->curr_pass <= vp8_context->num_passes && vp8_context->repak_pass_iter_val == 0))) {
            i965_encoder_vp8_pak_slice_level_brc(ctx, encoder_context, vp8_context->curr_pass);

            if (vp8_context->tpu_required)
                vp8_context->submit_batchbuffer = 1;
            else
                vp8_context->submit_batchbuffer = 0;
        } else {
            if (brc_enabled) {
                i965_encoder_vp8_read_pak_statistics(ctx, encoder_context, vp8_context->curr_pass);
            }

            pbuffer = i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);

            if (!pbuffer)
                return;

            pbuffer += 38;
            *pbuffer = 0x05000000;
            i965_map_gpe_resource(&vp8_context->pak_mpu_tpu_picture_state_buffer);

            vp8_context->submit_batchbuffer = 1;
        }
    }
}

static void
i965_encoder_vp8_pak_tpu_set_curbe(VADriverContextP ctx,
                                   struct encode_state *encode_state,
                                   struct intel_encoder_context *encoder_context,
                                   struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct vp8_tpu_curbe_data *pcmd = i965_gpe_context_map_curbe(gpe_context);
    VAEncPictureParameterBufferVP8 *pic_param = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
    VAQMatrixBufferVP8 *quant_param = (VAQMatrixBufferVP8 *)encode_state->q_matrix->buffer;

    if (!pcmd)
        return;

    memset(pcmd, 0, sizeof(*pcmd));

    pcmd->dw0.mbs_in_frame = vp8_context->frame_width_in_mbs * vp8_context->frame_height_in_mbs;

    pcmd->dw1.frame_type = pic_param->pic_flags.bits.frame_type;
    pcmd->dw1.enable_segmentation = pic_param->pic_flags.bits.segmentation_enabled;
    pcmd->dw1.rebinarization_frame_hdr = (vp8_context->repak_pass_iter_val ? 1 : 0);

    pcmd->dw1.refresh_entropy_p = pic_param->pic_flags.bits.refresh_entropy_probs;
    pcmd->dw1.mb_no_coeffiscient_skip = pic_param->pic_flags.bits.mb_no_coeff_skip;

    pcmd->dw3.max_qp = pic_param->clamp_qindex_high;
    pcmd->dw3.min_qp = pic_param->clamp_qindex_low;

    pcmd->dw4.loop_filter_level_segment0 = pic_param->loop_filter_level[0];
    pcmd->dw4.loop_filter_level_segment1 = pic_param->loop_filter_level[1];
    pcmd->dw4.loop_filter_level_segment2 = pic_param->loop_filter_level[2];
    pcmd->dw4.loop_filter_level_segment3 = pic_param->loop_filter_level[3];

    pcmd->dw5.quantization_index_segment0 = quant_param->quantization_index[0];
    pcmd->dw5.quantization_index_segment1 = quant_param->quantization_index[1];
    pcmd->dw5.quantization_index_segment2 = quant_param->quantization_index[2];
    pcmd->dw5.quantization_index_segment3 = quant_param->quantization_index[3];

    pcmd->dw6.pak_pass_num = (vp8_context->internal_rate_mode > 0 ? vp8_context->num_brc_pak_passes : 0) << 8;

    if (vp8_context->repak_pass_iter_val > 0) { // TODO: more check
        pcmd->dw7.skip_cost_delta_threshold = 100;
        pcmd->dw7.token_cost_delta_threshold = 50;
    } else {
        pcmd->dw7.skip_cost_delta_threshold = 0;
        pcmd->dw7.token_cost_delta_threshold = 0;
    }

    pcmd->dw12.pak_token_statistics_bti = VP8_BTI_TPU_PAK_TOKEN_STATISTICS;
    pcmd->dw13.token_update_flags_bti = VP8_BTI_TPU_TOKEN_UPDATE_FLAGS;
    pcmd->dw14.entropy_cost_table_bti = VP8_BTI_TPU_ENTROPY_COST_TABLE;
    pcmd->dw15.frame_header_bitstream_bti = VP8_BTI_TPU_HEADER_BITSTREAM;
    pcmd->dw16.default_token_probability_bti = VP8_BTI_TPU_DEFAULT_TOKEN_PROBABILITY;
    pcmd->dw17.picture_state_bti = VP8_BTI_TPU_PICTURE_STATE;
    pcmd->dw18.mpu_curbe_data_bti = VP8_BTI_TPU_MPU_CURBE_DATA;
    pcmd->dw19.header_meta_data_bti = VP8_BTI_TPU_HEADER_METADATA;
    pcmd->dw20.token_probability_bti = VP8_BTI_TPU_TOKEN_PROBABILITY;
    pcmd->dw21.pak_hardware_token_probability_pass1_bti = VP8_BTI_TPU_PAK_HW_PASS1_PROBABILITY;
    pcmd->dw22.key_frame_token_probability_bti = VP8_BTI_TPU_KEY_TOKEN_PROBABILITY;
    pcmd->dw23.updated_token_probability_bti = VP8_BTI_TPU_UPDATED_TOKEN_PROBABILITY;
    pcmd->dw24.pak_hardware_token_probability_pass2_bti = VP8_BTI_TPU_PAK_HW_PASS2_PROBABILITY;
    pcmd->dw25.kernel_debug_dump_bti = VP8_BTI_TPU_VME_DEBUG_STREAMOUT;
    pcmd->dw26.repak_decision_surface_bti = VP8_BTI_TPU_REPAK_DECISION;

    i965_gpe_context_unmap_curbe(gpe_context);
}

static void
i965_encoder_vp8_tpu_add_surfaces(VADriverContextP ctx,
                                  struct encode_state *encode_state,
                                  struct intel_encoder_context *encoder_context,
                                  struct i965_gpe_context *gpe_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    unsigned int size;
    unsigned char brc_enabled = (vp8_context->internal_rate_mode == I965_BRC_CBR ||
                                 vp8_context->internal_rate_mode == I965_BRC_VBR);

    // Pak token statistics
    size = VP8_TOKEN_STATISTICS_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_pak_token_statistics_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_TPU_PAK_TOKEN_STATISTICS);

    // Pak token Update flags
    size = VP8_COEFFS_PROPABILITIES_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_pak_token_update_flags_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_TOKEN_UPDATE_FLAGS);

    // Entropy cost
    size = VP8_ENTROPY_COST_TABLE_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_entropy_cost_table_buffer,
                                1,
                                size,
                                0,
                                VP8_BTI_TPU_ENTROPY_COST_TABLE);

    // Frame header
    size = VP8_FRAME_HEADER_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_frame_header_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_HEADER_BITSTREAM);

    // Default token token probability
    size = VP8_COEFFS_PROPABILITIES_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_default_token_probability_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_DEFAULT_TOKEN_PROBABILITY);

    // Picture state surface
    size = VP8_PICTURE_STATE_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_picture_state_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_PICTURE_STATE);

    // MPU Curbe info from TPU
    size = VP8_TOKEN_BITS_DATA_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_token_bits_data_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_MPU_CURBE_DATA);

    // Encoder CFG command surface
    size = VP8_HEADER_METADATA_SIZE;

    if (brc_enabled) {
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->brc_vp8_cfg_command_write_buffer,
                                    0,
                                    size,
                                    0,
                                    VP8_BTI_TPU_HEADER_METADATA);
    } else {
        i965_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &vp8_context->pak_mpu_tpu_picture_state_buffer,
                                    0,
                                    size,
                                    VP8_HEADER_METADATA_OFFSET,
                                    VP8_BTI_TPU_HEADER_METADATA);
    }

    // Current frame token probability
    size = VP8_COEFFS_PROPABILITIES_SIZE;
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_coeff_probs_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_TOKEN_PROBABILITY);

    // Hardware token probability pass 1
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_ref_coeff_probs_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_PAK_HW_PASS1_PROBABILITY);

    // key frame token probability
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_updated_token_probability_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_UPDATED_TOKEN_PROBABILITY);

    // update token probability
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_key_frame_token_probability_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_KEY_TOKEN_PROBABILITY);

    // Hardware token probability pass 2
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_hw_token_probability_pak_pass_2_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_PAK_HW_PASS2_PROBABILITY);

    // Repak Decision
    i965_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &vp8_context->pak_mpu_tpu_repak_decision_buffer,
                                0,
                                size,
                                0,
                                VP8_BTI_TPU_REPAK_DECISION);
}

static void
i965_encoder_vp8_pak_tpu(VADriverContextP ctx,
                         struct encode_state *encode_state,
                         struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_encoder_vp8_tpu_context *tpu_context = &vp8_context->tpu_context;
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct gpe_media_object_parameter media_object_param;
    struct i965_gpe_context *gpe_context;
    int media_function = VP8_MEDIA_STATE_TPU;

    gpe_context = &tpu_context->gpe_contexts[0];
    /* gpe->context_init(ctx, gpe_context); */
    gpe->reset_binding_table(ctx, gpe_context);

    if (!vp8_context->tpu_curbe_updated_in_brc_update)
        i965_encoder_vp8_pak_tpu_set_curbe(ctx, encode_state, encoder_context, gpe_context);

    i965_encoder_vp8_tpu_add_surfaces(ctx, encode_state, encoder_context, gpe_context);
    gpe->setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    i965_run_kernel_media_object(ctx, encoder_context, gpe_context, media_function, &media_object_param);
}

#define PAK_REFERENCE_BO(dst_bo, src_bo, is_ref_bo)     \
    do {                                                \
        dri_bo_unreference(dst_bo);                     \
        dst_bo = src_bo;                                \
        if (is_ref_bo)                                  \
            dri_bo_reference(dst_bo);                   \
    } while (0)

static void
i965_encoder_vp8_pak_pipeline_prepare(VADriverContextP ctx,
                                      struct encode_state *encode_state,
                                      struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct object_surface *obj_surface;
    struct object_buffer *obj_buffer;
    struct i965_coded_buffer_segment *coded_buffer_segment;
    dri_bo *bo;
    int i;

    /* reconstructed surface */
    obj_surface = encode_state->reconstructed_object;
    i965_check_alloc_surface_bo(ctx, obj_surface, 1, VA_FOURCC_NV12, SUBSAMPLE_YUV420);

    PAK_REFERENCE_BO(vp8_context->pre_deblocking_output.bo, obj_surface->bo, 1);
    PAK_REFERENCE_BO(vp8_context->post_deblocking_output.bo, obj_surface->bo, 1);

    /* set vp8 reference frames */
    for (i = 0; i < ARRAY_ELEMS(vp8_context->reference_surfaces); i++) {
        obj_surface = encode_state->reference_objects[i];

        if (obj_surface && obj_surface->bo) {
            PAK_REFERENCE_BO(vp8_context->reference_surfaces[i].bo, obj_surface->bo, 1);
        } else {
            PAK_REFERENCE_BO(vp8_context->reference_surfaces[i].bo, NULL, 0);
        }
    }

    /* input YUV surface */
    obj_surface = encode_state->input_yuv_object;
    PAK_REFERENCE_BO(vp8_context->uncompressed_picture_source.bo, obj_surface->bo, 1);

    /* coded buffer */
    obj_buffer = encode_state->coded_buf_object;
    bo = obj_buffer->buffer_store->bo;
    vp8_context->indirect_pak_bse_object.offset = I965_CODEDBUFFER_HEADER_SIZE;
    vp8_context->indirect_pak_bse_object.end_offset = ALIGN((obj_buffer->size_element - 0x1000), 0x1000);
    PAK_REFERENCE_BO(vp8_context->indirect_pak_bse_object.bo, bo, 1);

    /* set the internal flag to 0 to indicate the coded size is unknown */
    dri_bo_map(bo, 1);

    if (!bo->virtual)
        return;

    coded_buffer_segment = (struct i965_coded_buffer_segment *)bo->virtual;
    coded_buffer_segment->mapped = 0;
    coded_buffer_segment->codec = encoder_context->codec;
    coded_buffer_segment->status_support = 1;
    dri_bo_unmap(bo);
}

static VAStatus
i965_encoder_vp8_pak_pipeline_final(VADriverContextP ctx,
                                    struct encode_state *encode_state,
                                    struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct i965_encoder_vp8_tpu_context *tpu_context = &vp8_context->tpu_context;

    dri_bo_unreference(tpu_context->dynamic_buffer);
    tpu_context->dynamic_buffer = NULL;

    return VA_STATUS_SUCCESS;
}

#undef PAK_REFERENCE_BO

static VAStatus
i965_encoder_vp8_pak_pipeline(VADriverContextP ctx,
                              VAProfile profile,
                              struct encode_state *encode_state,
                              struct intel_encoder_context *encoder_context)
{
    struct i965_encoder_vp8_context *vp8_context = encoder_context->mfc_context;
    struct intel_batchbuffer *batch = encoder_context->base.batch;

    i965_encoder_vp8_pak_pipeline_prepare(ctx, encode_state, encoder_context);

    vp8_context->is_render_context = 0;
    vp8_context->submit_batchbuffer = 1;

    for (vp8_context->curr_pass = 0; vp8_context->curr_pass <= vp8_context->num_passes; vp8_context->curr_pass++) {
        vp8_context->tpu_required = ((vp8_context->curr_pass == (vp8_context->num_passes - 1) &&
                                      vp8_context->repak_pass_iter_val > 0) ||
                                     (vp8_context->curr_pass == vp8_context->num_passes &&
                                      vp8_context->repak_pass_iter_val == 0));

        if (vp8_context->submit_batchbuffer)
            intel_batchbuffer_start_atomic_bcs_override(batch, 0x1000, vp8_context->vdbox_idc);

        intel_batchbuffer_emit_mi_flush(batch);
        i965_encoder_vp8_pak_picture_level(ctx, encode_state, encoder_context);
        i965_encoder_vp8_pak_slice_level(ctx, encode_state, encoder_context);

        if (vp8_context->submit_batchbuffer) {
            intel_batchbuffer_end_atomic(batch);
            intel_batchbuffer_flush(batch);
        }

        if (vp8_context->tpu_required) {
            assert(vp8_context->submit_batchbuffer);
            i965_encoder_vp8_pak_tpu(ctx, encode_state, encoder_context);
        }
    }

    if (!vp8_context->is_first_frame && vp8_context->is_first_two_frame)
        vp8_context->is_first_two_frame = 0;

    vp8_context->is_first_frame = 0;
    vp8_context->tpu_curbe_updated_in_brc_update = 0;

    i965_encoder_vp8_pak_pipeline_final(ctx, encode_state, encoder_context);

    return VA_STATUS_SUCCESS;
}

static void
i965_encoder_vp8_pak_tpu_context_init(VADriverContextP ctx,
                                      struct i965_encoder_vp8_context *vp8_context,
                                      struct i965_encoder_vp8_tpu_context *tpu_context)
{
    struct i965_gpe_table *gpe = vp8_context->gpe_table;
    struct i965_gpe_context *gpe_context = NULL;
    struct vp8_encoder_kernel_parameters kernel_params;
    struct vp8_encoder_scoreboard_parameters scoreboard_params;
    int i;

    kernel_params.curbe_size = sizeof(struct vp8_tpu_curbe_data);
    kernel_params.inline_data_size = 0;
    kernel_params.external_data_size = 0;

    memset(&scoreboard_params, 0, sizeof(scoreboard_params));
    scoreboard_params.mask = 0xFF;
    scoreboard_params.enable = vp8_context->use_hw_scoreboard;
    scoreboard_params.type = vp8_context->use_hw_non_stalling_scoreborad;

    for (i = 0; i < NUM_VP8_TPU; i++) {
        gpe_context = &tpu_context->gpe_contexts[i];
        i965_encoder_vp8_gpe_context_init_once(ctx, gpe_context, &kernel_params, vp8_context->idrt_entry_size);
        i965_encoder_vp8_gpe_context_vfe_scoreboard_init(gpe_context, &scoreboard_params);
        gpe->load_kernels(ctx,
                          gpe_context,
                          &vp8_kernels_tpu[i],
                          1);
    }
}

static void
i965_encoder_vp8_pak_kernels_context_init(VADriverContextP ctx,
                                          struct intel_encoder_context *encoder_context,
                                          struct i965_encoder_vp8_context *vp8_context)
{
    i965_encoder_vp8_pak_tpu_context_init(ctx, vp8_context, &vp8_context->tpu_context);
}

static VAStatus
i965_encoder_vp8_get_status(VADriverContextP ctx,
                            struct intel_encoder_context *encoder_context,
                            struct i965_coded_buffer_segment *coded_buffer_segment)
{
    struct vp8_encode_status *encode_state = (struct vp8_encode_status *)coded_buffer_segment->codec_private_data;

    coded_buffer_segment->base.size = encode_state->bitstream_byte_count_per_frame;

    return VA_STATUS_SUCCESS;
}

Bool
i965_encoder_vp8_pak_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
    /* VME & PAK share the same context */
    struct i965_encoder_vp8_context *vp8_context = encoder_context->vme_context;

    assert(vp8_context);
    i965_encoder_vp8_pak_kernels_context_init(ctx, encoder_context, vp8_context);

    encoder_context->mfc_context = vp8_context;
    encoder_context->mfc_context_destroy = i965_encoder_vp8_pak_context_destroy;
    encoder_context->mfc_pipeline = i965_encoder_vp8_pak_pipeline;
    encoder_context->mfc_brc_prepare = i965_encoder_vp8_pak_pre_pipeline;
    encoder_context->get_status = i965_encoder_vp8_get_status;

    return True;
}
