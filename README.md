# vadl
GPU video acceleration library for deep learning inference and training

## notes
build ffmpeg example
```bash
cd build/ffmpeg
../../FFmpeg/configure --enable-debug=3 --disable-optimizations --enable-libx264 --enable-libx265 --enable-gpl
make -j4
make examples
```
https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples
