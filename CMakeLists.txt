cmake_minimum_required(VERSION 3.10)

project(dreams)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
add_compile_options(-Wno-nullability-completeness)

option(DREAMS_BUILD_EXAMPLES "Build examples" ON)
option(DREAMS_BUILD_WITH_POSIX_THREADS "Build with POSIX threads" OFF)
option(DREAMS_ALIAS_MODULES "CMake: Alias C++ module libraries" ON)
option(DREAMS_FIND_PACKAGES "CMake: Find packages (if you disable this, you need to provide them)" ON)

if(DREAMS_BUILD_WITH_POSIX_THREADS)
  add_compile_options(-pthread)
endif()

include(FetchContent)

# The following targets are required (one way or another):
# - SDL2::SDL2
# - SDL2_image::SDL2_image
# - SDL2_mixer::SDL2_mixer
# - Freetype::Freetype
# - glm::glm
if(DREAMS_FIND_PACKAGES)
  find_package(SDL2 REQUIRED)
  find_package(SDL2_image REQUIRED)
  find_package(SDL2_mixer REQUIRED)
  find_package(Freetype REQUIRED)
  find_package(glm REQUIRED)
  find_package(Vulkan REQUIRED)
endif()

FetchContent_Declare(
  Vulkan-Hpp
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
  GIT_TAG        v1.4.321
  GIT_SUBMODULES_RECURSE FALSE
)
set(VULKAN_HPP_BUILD_WITH_LOCAL_VULKAN_HPP ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Vulkan-Hpp)

FetchContent_Declare(
  VulkanMemoryAllocator-Hpp
  GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
  GIT_TAG        73cdd838c425637c874d343ab0ceba5148189cbf
  GIT_SUBMODULES_RECURSE FALSE
  PATCH_COMMAND  patch --forward -i ${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator-Hpp.patch || true
)
FetchContent_MakeAvailable(VulkanMemoryAllocator-Hpp)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
  EXCLUDE_FROM_ALL
)
set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

add_subdirectory(third_party)
add_subdirectory(shaders)
add_subdirectory(src)

if(DREAMS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
