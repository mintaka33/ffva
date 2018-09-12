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
    VideoAccel();
    ~VideoAccel();

private:
    AVBufferRef *hwDeviceCtx_ = nullptr;
    AVFormatContext *inputCtx_ = nullptr;
    AVCodecContext *decoderCtx_ = nullptr;
    AVStream *video_ = nullptr;
    AVCodec *decoder_ = nullptr;
};

