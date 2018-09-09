cmake_minimum_required(VERSION 2.8)

project( vadec )

#MESSAGE( STATUS "PROJECT_SOURCE_DIR = " ${PROJECT_SOURCE_DIR} )
#MESSAGE( STATUS "CMAKE_SOURCE_DIR = " ${CMAKE_SOURCE_DIR} )

#find_path( FFMPEG_INCLUDE libavcodec HINTS /usr/lib/x86_64-linux-gnu/include)
#MESSAGE( STATUS "FFMPEG_INCLUDE = " ${FFMPEG_INCLUDE} )

#find_library(FFMPEG_LIB_AVCODEC avcodec HINTS /usr/lib/x86_64-linux-gnu/lib)
#find_library(FFMPEG_LIB_AVFORMAT avformat HINTS /usr/lib/x86_64-linux-gnu/lib)
#find_library(FFMPEG_LIB_AVFILTER avfilter HINTS /usr/lib/x86_64-linux-gnu/lib)
#find_library(FFMPEG_LIB_AVDEVICE avdevice HINTS /usr/lib/x86_64-linux-gnu/lib)
#find_library(FFMPEG_LIB_AVUTIL avutil PATHS /usr/lib/x86_64-linux-gnu/lib)
#MESSAGE( STATUS "FFMPEG_LIB_AVCODEC = " ${FFMPEG_LIB_AVCODEC} )
#MESSAGE( STATUS "FFMPEG_LIB_AVFORMAT = " ${FFMPEG_LIB_AVFORMAT} )
#MESSAGE( STATUS "FFMPEG_LIB_AVFILTER = " ${FFMPEG_LIB_AVFILTER} )
#MESSAGE( STATUS "FFMPEG_LIB_AVDEVICE = " ${FFMPEG_LIB_AVDEVICE} )
#MESSAGE( STATUS "FFMPEG_LIB_AVUTIL = " ${FFMPEG_LIB_AVUTIL} )

set (CMAKE_CXX_STANDARD 11)
#set (CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")

add_executable( vadec vadec.cpp )

#include_directories (vadec PRIVATE "${FFMPEG_INCLUDE}")
#target_link_libraries( vadec PRIVATE ${FFMPEG_LIB_AVCODEC} ${FFMPEG_LIB_AVFORMAT} ${FFMPEG_LIB_AVFILTER} ${FFMPEG_LIB_AVDEVICE} ${FFMPEG_LIB_AVUTIL} )

include_directories (vadec /usr/local/include)
#link_directories(/usr/local/lib)
target_link_libraries( vadec avutil avformat avcodec avfilter avdevice)
