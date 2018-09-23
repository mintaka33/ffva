
#include "accel.hpp"
#include "frame.hpp"

int main (int argc, char** argv)
{
    const char* infile;
    if (argc == 1) {
         infile = "../../test/test.264";
    }
    else if (argc == 2) {
        infile = argv[1];
    }
    else {
        printf("arguments error!\n");
        return -1;
    }

    VAccel accel(infile);

    if(accel.init() != 0) {
        printf("VAccel init failed!\n");
        return -1;
    }

    VFrame vf;
    for (int i=0; i<40; i++) {
        if(accel.getFrame(&vf) != 0) {
            printf("VAccel init failed!\n");
            return -1;
        }
        vf.saveFile();
    }

    printf("test done!\n");
    return 0;
}