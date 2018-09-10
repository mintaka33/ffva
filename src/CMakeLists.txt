cmake_minimum_required(VERSION 2.8)

project(vadec)

set (CMAKE_CXX_STANDARD 11)

include_directories (vadec /usr/local/include)
# link_directories must be put before add_executable
link_directories(/usr/local/lib/)

set (FFMPEG_LIBS avutil avformat avcodec avfilter avdevice)

add_executable(vadec vadec.cpp videoaccel.cpp videoaccel.hpp)
target_link_libraries(vadec ${FFMPEG_LIBS})

add_executable(vaenc vaenc.cpp)
target_link_libraries(vaenc ${FFMPEG_LIBS})

add_executable(vavpp vavpp.cpp)
target_link_libraries(vavpp ${FFMPEG_LIBS})
