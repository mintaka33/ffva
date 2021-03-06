#pragma once

#include <stdint.h>

class VFrame
{
public:
    VFrame();
    ~VFrame();

    uint8_t* getBuf() { return buffer_; }
    int32_t getSize() { return size_; }
    int32_t getWidth() { return width_; }
    int32_t getHeight() { return height_; }

    void allocate(int32_t width, int32_t height);
    void saveFile();

private:
    uint8_t *buffer_ = nullptr;
    bool firstWrite_ = true;
    int32_t size_ = 0;
    int32_t width_ = 0;
    int32_t height_ = 0;
};