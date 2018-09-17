
#include "frame.hpp"

VFrame::VFrame()
{
}

VFrame::~VFrame()
{
    if (buffer_) {
        delete [] buffer_;
        buffer_ = nullptr;
    }
}
