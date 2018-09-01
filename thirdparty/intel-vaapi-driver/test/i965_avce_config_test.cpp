/*
 * Copyright (C) 2016 Intel Corporation. All Rights Reserved.
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

#include "i965_config_test.h"

namespace AVC {
namespace Encode {

VAStatus ProfileNotSupported()
{
    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus EntrypointNotSupported()
{
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
}

//H264*NotSupported functions report properly if profile is not supported or
//only entrypoint is not supported
VAStatus H264NotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_DECODING(i965)
        && !HAS_LP_H264_ENCODING(i965)
        && !HAS_FEI_H264_ENCODING(i965)
        && !HAS_H264_PREENC(i965))
        return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus H264LPNotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_DECODING(i965)
        && !HAS_H264_ENCODING(i965)
        && !HAS_FEI_H264_ENCODING(i965)
        && !HAS_H264_PREENC(i965))
        return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus H264FEINotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_DECODING(i965)
        && !HAS_H264_ENCODING(i965)
        && !HAS_LP_H264_ENCODING(i965)
        && !HAS_H264_PREENC(i965))
        return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus H264PreEncodeNotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_DECODING(i965)
        && !HAS_H264_ENCODING(i965)
        && !HAS_LP_H264_ENCODING(i965)
        && !HAS_FEI_H264_ENCODING(i965))
         return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus H264MVCNotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_MVC_DECODING_PROFILE(i965, VAProfileH264MultiviewHigh))
        return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus H264StereoNotSupported()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (!HAS_H264_MVC_DECODING_PROFILE(i965, VAProfileH264StereoHigh))
        return ProfileNotSupported();

    return EntrypointNotSupported();
}

VAStatus HasEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (HAS_H264_ENCODING(i965))
        return VA_STATUS_SUCCESS;

    return H264NotSupported();
}

VAStatus HasLPEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (IS_SKL(i965->intel.device_info))
        return VA_STATUS_SUCCESS;

    if (HAS_LP_H264_ENCODING(i965))
        return VA_STATUS_SUCCESS;

    return H264LPNotSupported();
}


VAStatus HasFEIEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (IS_SKL(i965->intel.device_info))
        return VA_STATUS_SUCCESS;

    if (HAS_FEI_H264_ENCODING(i965))
        return VA_STATUS_SUCCESS;

    return H264FEINotSupported();
}

VAStatus HasPreEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (IS_SKL(i965->intel.device_info))
        return VA_STATUS_SUCCESS;

    if (HAS_H264_PREENC(i965))
        return VA_STATUS_SUCCESS;

    return H264PreEncodeNotSupported();
}

VAStatus HasMVCEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (HAS_H264_MVC_ENCODING(i965))
        return VA_STATUS_SUCCESS;

    return H264MVCNotSupported();
}

VAStatus HasStereoEncodeSupport()
{
    I965TestEnvironment *env(I965TestEnvironment::instance());
    EXPECT_PTR(env);

    struct i965_driver_data *i965(*env);
    EXPECT_PTR(i965);

    if (HAS_H264_MVC_ENCODING(i965))
        return VA_STATUS_SUCCESS;

    return H264StereoNotSupported();
}

static const std::vector<ConfigTestInput> inputs = {
    {VAProfileH264ConstrainedBaseline, VAEntrypointEncSlice, &HasEncodeSupport},
    {VAProfileH264ConstrainedBaseline, VAEntrypointEncSliceLP, &HasLPEncodeSupport},
    {VAProfileH264ConstrainedBaseline, VAEntrypointEncPicture, &H264NotSupported},
    {VAProfileH264ConstrainedBaseline, VAEntrypointFEI, &HasFEIEncodeSupport},
    {VAProfileH264ConstrainedBaseline, VAEntrypointStats, &HasPreEncodeSupport},

    {VAProfileH264Main, VAEntrypointEncSlice, &HasEncodeSupport},
    {VAProfileH264Main, VAEntrypointEncSliceLP, &HasLPEncodeSupport},
    {VAProfileH264Main, VAEntrypointEncPicture, &H264NotSupported},
    {VAProfileH264Main, VAEntrypointFEI, &HasFEIEncodeSupport},
    {VAProfileH264Main, VAEntrypointStats, &HasPreEncodeSupport},

    {VAProfileH264High, VAEntrypointEncSlice, &HasEncodeSupport},
    {VAProfileH264High, VAEntrypointEncSliceLP, &HasLPEncodeSupport},
    {VAProfileH264High, VAEntrypointEncPicture, &H264NotSupported},
    {VAProfileH264High, VAEntrypointFEI, &HasFEIEncodeSupport},
    {VAProfileH264High, VAEntrypointStats, &HasPreEncodeSupport},

    {VAProfileH264MultiviewHigh, VAEntrypointEncSlice, &HasMVCEncodeSupport},
    {VAProfileH264MultiviewHigh, VAEntrypointEncSliceLP, &H264MVCNotSupported},
    {VAProfileH264MultiviewHigh, VAEntrypointEncPicture, &H264MVCNotSupported},
    {VAProfileH264MultiviewHigh, VAEntrypointFEI, &H264MVCNotSupported},
    {VAProfileH264MultiviewHigh, VAEntrypointStats, &H264MVCNotSupported},

    {VAProfileH264StereoHigh, VAEntrypointEncSlice, &HasStereoEncodeSupport},
    {VAProfileH264StereoHigh, VAEntrypointEncSliceLP, &H264StereoNotSupported},
    {VAProfileH264StereoHigh, VAEntrypointEncPicture, &H264StereoNotSupported},
    {VAProfileH264StereoHigh, VAEntrypointFEI, &H264StereoNotSupported},
    {VAProfileH264StereoHigh, VAEntrypointStats, &H264MVCNotSupported},
};

INSTANTIATE_TEST_CASE_P(
    AVCEncode, I965ConfigTest, ::testing::ValuesIn(inputs));

} // namespace Encode
} // namespace AVC
