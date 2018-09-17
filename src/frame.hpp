#pragma once

#include <stdint.h>

class VFrame
{
public:
    VFrame();
    ~VFrame();

    uint8_t* getBuf() {return buffer_;}
    void allocate(int32_t size) {
        size_ = size;
        buffer_ = new uint8_t[size];
    }

private:
    uint8_t *buffer_ = nullptr;
    int32_t size_ = 0;
};