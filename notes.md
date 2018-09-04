# Notes

## reference
https://trac.ffmpeg.org/wiki/HWAccelIntro

https://trac.ffmpeg.org/wiki/Hardware/VAAPI

## build ffmpeg example
```bash
cd build/ffmpeg
../../FFmpeg/configure --enable-debug=3 --disable-optimizations --enable-libx264 --enable-libx265 --enable-gpl
make -j4
make examples
```

## command line examples
ffmpeg hw decode+vpp
```bash
./ffmpeg -y -hwaccel vaapi -hwaccel_output_format vaapi -vaapi_device /dev/dri/renderD128 \
-i ~/test.264 -vframes 1 -vf scale_vaapi=w=640:h=360,hwdownload,format=yuv420p out.yuv
```

ffmpeg hw decode+vpp+encode
```bash
./ffmpeg -y -v verbose -benchmark -hwaccel vaapi -hwaccel_output_format vaapi -vaapi_device /dev/dri/renderD128 \
 -i ~/test.264 -vframes 100 -vf scale_vaapi=w=640:h=360 -c:v h264_vaapi -b:v 1M /tmp/output.mp4
```