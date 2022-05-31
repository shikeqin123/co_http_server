cmake_minimum_required(VERSION 2.8)
project(co_http_server)

find_package(OpenSSL REQUIRED)

#包含头文件
include_directories(libco)

set(SSL_LIBS
    OpenSSL::Crypto OpenSSL::SSL
)

set(SRCS
    HttpData.cpp
    Utils.cpp
)

add_executable(${PROJECT_NAME} main.cpp ${SRCS})
#包含动态库，静态库
target_link_libraries(co_http_server colib_static pthread dl ${SSL_LIBS})

add_executable(co_https_server co_https_server.cpp ${SRCS})
target_link_libraries(co_https_server colib_static pthread dl ${SSL_LIBS})

add_subdirectory(libco)
add_subdirectory(tests)