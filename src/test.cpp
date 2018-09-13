
#include "videoaccel.hpp"

int main (int argc, char** argv)
{
    const char* infile;
    if (argc == 1)
         infile = "../../test/test.264";
    else if (argc == 2)
        infile = argv[1];
    else 
    {
        printf("arguments error!\n");
        return -1;
    }

    VideoAccel accel(infile);

    if(accel.init() != 0)
    {
        printf("VideoAccel init failed!\n");
        return -1;
    }

    printf("test done!\n");
    return 0;
}