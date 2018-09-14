#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
}

class VideoAccel
{
public:
    VideoAccel(const char* inf, const char* outf="out.yuv", const char* type="vaapi");
    ~VideoAccel();

    int init();

private:
    const char* vatype_;
    const char* infile_;
    const char* outfile_;
    AVBufferRef *hwDeviceCtx_ = nullptr;
    AVFormatContext *inputCtx_ = nullptr;
    AVCodecContext *decoderCtx_ = nullptr;
    AVStream *video_ = nullptr;
    AVCodec *decoder_ = nullptr;
    int stream_ = -1;
};

