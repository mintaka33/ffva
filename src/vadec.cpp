
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
    const char* hwtype = "vaapi";
    const char* infile = "~/test.264";
    AVFormatContext *input_ctx = nullptr;
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    type = av_hwdevice_find_type_by_name(hwtype);
    if (type == AV_HWDEVICE_TYPE_NONE)
        return -1;

    int ret =avformat_open_input(&input_ctx, infile, nullptr, nullptr);
    if ( ret != 0) {
        fprintf(stderr, "Cannot open input file %s\n", infile);
        return -1;
    }
    
    printf("va sample decoder\n");
    return 0;
}