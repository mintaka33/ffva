# build instructions

## build ffmpeg
```bash
# prerequisite
sudo apt update -qq && sudo apt-get -y install \
  autoconf \
  automake \
  build-essential \
  cmake \
  git \
  libass-dev \
  libfreetype6-dev \
  libsdl2-dev \
  libtool \
  libvorbis-dev \
  libxcb1-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  pkg-config \
  texinfo \
  wget \
  zlib1g-dev \
  nasm

sudo apt install libx264-dev libx265-dev

cd build
mkdir ffmpeg && cd ffmpeg
../../thirdparty/FFmpeg/configure --enable-debug=3 --disable-optimizations \
--enable-libx264 --enable-libx265 --enable-gpl
make -j$(nproc)
# build ffmpeg examples 
make examples
```

## build libva
```bash
# prerequisite
sudo apt install autoconf libtool libdrm-dev xorg xorg-dev \
openbox libx11-dev libgl1-mesa-glx libgl1-mesa-dev

cd build
mkdir libva && cd libva
../../thirdparty/libva/autogen.sh CFLAGS=-g CXXFLAGS=-g
make -j$(nproc)
sudo make install
```

## build libva-utils
```bash
cd build
mkdir utils && cd utils
../../thirdparty/libva-utils/autogen.sh CFLAGS=-g CXXFLAGS=-g
make -j$(nproc)
```

## build intel-vaapi-driver
```bash
cd build
mkdir vaapi_driver && cd vaapi_driver
../../thirdparty/intel-vaapi-driver/autogen.sh CFLAGS=-g CXXFLAGS=-g
make -j$(nproc)
sudo make install
```

# set environment variables
```bash
export LD_LIBRARY_PATH=/usr/local/lib
export LIBVA_DRIVER_NAME=i965
export LIBVA_DRIVER_PATH=/usr/local/lib/dri
```

# command line examples
ffmpeg vaapi decode + vpp scaling
```bash
./ffmpeg -y -hwaccel vaapi -hwaccel_output_format vaapi -vaapi_device /dev/dri/renderD128 \
-i ~/test.264 -vframes 1 -vf scale_vaapi=w=640:h=360,hwdownload,format=yuv420p /tmp/out.yuv
```