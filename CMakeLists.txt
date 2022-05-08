cmake_minimum_required(VERSION 2.8)
project(co_http_server)

#包含头文件
include_directories(libco)

set(SRCS
    HttpData.cpp
    Utils.cpp
)

add_executable(${PROJECT_NAME} main.cpp ${SRCS})
#包含动态库，静态库
target_link_libraries(co_http_server colib_static pthread dl)

add_subdirectory(libco)
add_subdirectory(tests)