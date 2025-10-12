cmake_minimum_required(VERSION 3.16)
project(op5u_2d_engine LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

add_executable(op5u src/main.cpp)

target_include_directories(op5u PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_IMAGE_INCLUDE_DIRS}
)
target_link_libraries(op5u PRIVATE
  ${SDL2_LIBRARIES}
  ${SDL2_IMAGE_LIBRARIES}
  GLESv2
)
