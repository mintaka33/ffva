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