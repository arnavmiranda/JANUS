cmake_minimum_required(VERSION 3.10)
project(janus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

add_executable(janus src/main.cpp src/JanusFS.cpp)

target_include_directories(janus PRIVATE ${FUSE3_INCLUDE_DIRS})
target_link_options(janus PRIVATE ${FUSE3_LDFLAGS})
target_link_libraries(janus PRIVATE ${FUSE3_LIBRARIES})
