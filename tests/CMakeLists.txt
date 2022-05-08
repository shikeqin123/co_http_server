include_directories(libco)
include_directories(..)

set(SRC_TEMP
    ../HttpData.cpp
    ../Utils.cpp
)

add_executable(http_test http_test.cpp ${SRC_TEMP})
target_link_libraries(http_test colib_static pthread dl)