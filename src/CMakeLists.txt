cmake_minimum_required(VERSION 2.8)

project( vadec )

MESSAGE( STATUS "PROJECT_SOURCE_DIR = " ${PROJECT_SOURCE_DIR} )
MESSAGE( STATUS "CMAKE_SOURCE_DIR = " ${CMAKE_SOURCE_DIR} )

find_path( FFMPEG_INCLUDE libavcodec )
MESSAGE( STATUS "FFMPEG_INCLUDE = " ${FFMPEG_INCLUDE} )
include_directories ("${FFMPEG_INCLUDE}")

find_library(FFMPEG_LIB_AVCODEC avcodec)
find_library(FFMPEG_LIB_AVFORMAT avformat)
find_library(FFMPEG_LIB_AVFILTER avfilter)
find_library(FFMPEG_LIB_AVDEVICE avdevice)
find_library(FFMPEG_LIB_AVUTIL avutil)
MESSAGE( STATUS "FFMPEG_LIB_AVCODEC = " ${FFMPEG_LIB_AVCODEC} )
MESSAGE( STATUS "FFMPEG_LIB_AVFORMAT = " ${FFMPEG_LIB_AVFORMAT} )
MESSAGE( STATUS "FFMPEG_LIB_AVFILTER = " ${FFMPEG_LIB_AVFILTER} )
MESSAGE( STATUS "FFMPEG_LIB_AVDEVICE = " ${FFMPEG_LIB_AVDEVICE} )
MESSAGE( STATUS "FFMPEG_LIB_AVUTIL = " ${FFMPEG_LIB_AVUTIL} )

# need set C++11 to use OpenCV
set (CMAKE_CXX_STANDARD 11)

add_executable( vadec vadec.cpp )

target_link_libraries( vadec PUBLIC ${FFMPEG_LIB_AVCODEC} ${FFMPEG_LIB_AVFORMAT} ${FFMPEG_LIB_AVFILTER} ${FFMPEG_LIB_AVDEVICE} ${FFMPEG_LIB_AVUTIL} )

#link_directories(/usr/local/lib)
#target_link_libraries( vadec avcodec avformat avfilter avdevice avutil)