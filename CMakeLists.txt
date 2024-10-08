cmake_minimum_required(VERSION 3.10)

project(dreams)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
add_compile_options(-Wno-nullability-completeness)

include(FetchContent)

find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(Freetype REQUIRED)

FetchContent_Declare(
  VulkanMemoryAllocator-Hpp
  GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
  GIT_TAG        73cdd838c425637c874d343ab0ceba5148189cbf
  GIT_SUBMODULES_RECURSE FALSE
  PATCH_COMMAND  patch --forward -i ${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator-Hpp.patch || true
)
FetchContent_MakeAvailable(VulkanMemoryAllocator-Hpp)

add_subdirectory(third_party)
add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(examples)
