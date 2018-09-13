#include "videoaccel.hpp"

VideoAccel::VideoAccel(const char* inf, const char* outf, const char* type) :
    infile_(inf),
    outfile_(outf),
    vatype_(type)
{
}

VideoAccel::~VideoAccel()
{
}

int VideoAccel::init()
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

    return 0;
}