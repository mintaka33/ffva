#include "accel.hpp"

static enum AVPixelFormat hwPixFmt;

static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hwPixFmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

VAccel::VAccel(const char* inf, const char* outf, const char* type) :
    infile_(inf),
    outfile_(outf),
    vatype_(type)
{
}

VAccel::~VAccel()
{
}

int VAccel::init()
{
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    type = av_hwdevice_find_type_by_name(vatype_);
    if (type == AV_HWDEVICE_TYPE_NONE)
        return -1;

    if ( avformat_open_input(&inputCtx_, infile_, nullptr, nullptr) != 0) 
    {
        fprintf(stderr, "Cannot open input file %s\n", infile_);
        return -1;
    }

    if (avformat_find_stream_info(inputCtx_, NULL) < 0) 
    {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    stream_ = av_find_best_stream(inputCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder_, 0);
    if (stream_ < 0) 
    {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }

    for (int i = 0; true; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder_, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder_->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hwPixFmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoderCtx_ = avcodec_alloc_context3(decoder_)))
        return AVERROR(ENOMEM);

    video_ = inputCtx_->streams[stream_];
    if (avcodec_parameters_to_context(decoderCtx_, video_->codecpar) < 0)
        return -1;

    decoderCtx_->get_format  = getHwFormat;

    if (av_hwdevice_ctx_create(&hwDeviceCtx_, type, NULL, NULL, 0) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return -1;
    }
    decoderCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);

    if (avcodec_open2(decoderCtx_, decoder_, NULL) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", stream_);
        return -1;
    }

    return 0;
}

int VAccel::getFrame(VFrame* f)
{
    int ret = 0;

    ret = decode(f);

    return ret;
}

int VAccel::decode(VFrame* f)
{
    AVFrame *frame = nullptr, *sw_frame = nullptr;
    AVFrame *tmp_frame = nullptr;
    uint8_t *buffer = nullptr;
    int size;
    int ret = 0;

    if (av_read_frame(inputCtx_, &packet_) < 0)
        return -1;

    if (stream_ != packet_.stream_index)
        return -1;

    ret = avcodec_send_packet(decoderCtx_, &packet_);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (1) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(decoderCtx_, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        if (frame->format == hwPixFmt) {
            /* retrieve data from GPU to CPU */
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
        } else
            tmp_frame = frame;

        size = av_image_get_buffer_size((AVPixelFormat)tmp_frame->format, tmp_frame->width,
                                        tmp_frame->height, 1);
        
        if (!f->getBuf()) {
            f->allocate(tmp_frame->width, tmp_frame->height);
            buffer = f->getBuf();
        }

        ret = av_image_copy_to_buffer(buffer, size,
                                      (const uint8_t * const *)tmp_frame->data,
                                      (const int *)tmp_frame->linesize, (AVPixelFormat)tmp_frame->format,
                                      tmp_frame->width, tmp_frame->height, 1);
        if (ret < 0) {
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        if (ret < 0)
            return ret;
    }

    return 0;
}