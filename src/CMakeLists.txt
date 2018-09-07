cmake_minimum_required(VERSION 2.8)

project( vadec )

MESSAGE( STATUS "PROJECT_SOURCE_DIR = " ${PROJECT_SOURCE_DIR} )
MESSAGE( STATUS "CMAKE_SOURCE_DIR = " ${CMAKE_SOURCE_DIR} )

find_path( FFMPEG_AVCODEC_INCLUDE libavcodec/avcodec.h )
find_library(FFMPEG_AVCODEC_LIB avcodec)
MESSAGE( STATUS "FFMPEG_AVCODEC_INCLUDE = " ${FFMPEG_AVCODEC_INCLUDE} )
MESSAGE( STATUS "FFMPEG_AVCODEC_LIB = " ${FFMPEG_AVCODEC_LIB} )

# need set C++11 to use OpenCV
set (CMAKE_CXX_STANDARD 11)

add_executable( vadec vadec.cpp )

target_include_directories(vadec PRIVATE ${FFMPEG_AVCODEC_INCLUDE} )
target_link_libraries(vadec PRIVATE ${FFMPEG_AVCODEC_LIB} )
