project(dvrcms)
cmake_minimum_required(VERSION 3.6)

if(LIBAV_PATH)
    set(LIBAV_LIB_PATH ${LIBAV_PATH}/lib)
    set(LIBAV_INCLUDE_PATH ${LIBAV_PATH}/include)
else()
    set(LIBAV_LIB_PATH /usr/lib/ffmpeg2.8/)
    set(LIBAV_INCLUDE_PATH /usr/include/ffmpeg2.8/)
endif()

include_directories(${LIBAV_INCLUDE_PATH})

link_directories(${LIBAV_LIB_PATH})

set(CROSSLIB_PORT "linux")
include(crosslib/crosslib.cmake)

set(NATIVELIB_PORT "linux")
include(nativelib/nativelib.cmake)

include(libkdav/libkdav.cmake)

add_executable(dvrcms
        src/main.cpp src/DVRCMS.cpp)

add_definitions(-ggdb)

target_link_libraries(dvrcms kdav nativelib crosslib -lSDL -lcrypto)

if(RPI)
    include_directories(/opt/vc/include)
    include_directories(/opt/vc/include/interface/vcos/pthreads)
    link_directories(/opt/vc/lib/)

    include_directories(/opt/vc/include/)
    include_directories(/opt/vc/include/interface/vcos/pthreads/)
    link_directories(/opt/vc/lib)
    target_link_libraries(dvrcms -lbcm_host -lmmal -lmmal_vc_client -lmmal_core -lmmal_components -lmmal_util)
endif()


set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread")

target_compile_features(dvrcms PRIVATE cxx_range_for)
