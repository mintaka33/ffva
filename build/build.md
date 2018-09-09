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
--enable-libx264 --enable-libx265 --enable-gpl --enable-shared --disable-static --disable-stripping
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

## vadl
```bash
cd build
mkdir bin && cd bin
cmake ../../src -DCMAKE_VERBOSE_MAKEFILE=ON
make
```
