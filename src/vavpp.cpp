#include <stdio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}

int main(int argc, char** argv)
{
    printf("va video processor\n");
    return 0;
}