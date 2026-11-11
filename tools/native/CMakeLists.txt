# Built into the Android converter app (and by tools/xnbfile.py on a PC).
cmake_minimum_required(VERSION 3.18)
project(s3dxnb CXX)
set(CMAKE_CXX_STANDARD 17)
set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../source)
add_library(s3dxnb SHARED s3dxnb.cpp ${SRC}/setup/lzx.cpp ${SRC}/setup/xnb.cpp)
target_include_directories(s3dxnb PRIVATE ${SRC})
target_compile_options(s3dxnb PRIVATE -O2)
