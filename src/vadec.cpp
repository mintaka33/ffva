
#include <stdio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
}

static enum AVPixelFormat hw_pix_fmt;

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int main(int argc, char** argv)
{
    const char* hwtype = "vaapi";
    const char* infile = "/tmp/test.264";
    int video_stream = -1;
    AVBufferRef *hw_device_ctx = nullptr;
    AVFormatContext *input_ctx = nullptr;
    AVCodecContext *decoder_ctx = nullptr;
    AVStream *video = nullptr;
    AVCodec *decoder = nullptr;
    AVPacket packet = {};
    FILE *output_file = nullptr;
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    int ret = 0;

    type = av_hwdevice_find_type_by_name(hwtype);
    if (type == AV_HWDEVICE_TYPE_NONE)
        return -1;

    if ( avformat_open_input(&input_ctx, infile, nullptr, nullptr) != 0) {
        fprintf(stderr, "Cannot open input file %s\n", infile);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    video_stream = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (video_stream < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }

    for (int i = 0; true; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    decoder_ctx->get_format  = get_hw_format;

    if (av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return -1;
    }
    decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    if (avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    
    printf("va sample decoder\n");
    return 0;
}