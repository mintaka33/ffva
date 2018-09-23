
#include "frame.hpp"
#include <fstream>

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

void VFrame::allocate(int32_t width, int32_t height) 
{
    width_ = width;
    height_ = height;
    size_ = width * height * 3 / 2;
    buffer_ = new uint8_t[size_];
}

void VFrame::saveFile()
{
    if (buffer_ && size_ > 0){
        std::ofstream f;
        f.open("out.yuv", std::ios::binary | std::ios::app);
        if (f.is_open()) {
            f.write((const char*)buffer_, size_);
            f.flush();
            f.close();
        }
    }
}