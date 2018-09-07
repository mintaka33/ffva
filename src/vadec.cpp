
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

int main(int argc, char** argv)
{
    AVFormatContext *input_ctx = NULL;
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    type = av_hwdevice_find_type_by_name("h264_vaapi");
    if (type == AV_HWDEVICE_TYPE_NONE)
        return -1;

    printf("va sample decoder\n");
    return 0;
}