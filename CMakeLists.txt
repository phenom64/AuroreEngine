# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

project(auroreengine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(_auroreengine_clang_scan_deps_names clang-scan-deps)
  if(CMAKE_CXX_COMPILER_VERSION)
    string(REGEX MATCH "^[0-9]+" _auroreengine_clang_major "${CMAKE_CXX_COMPILER_VERSION}")
    if(_auroreengine_clang_major)
      list(PREPEND _auroreengine_clang_scan_deps_names "clang-scan-deps-${_auroreengine_clang_major}")
    endif()
  endif()
  find_program(CLANG_SCAN_DEPS_EXECUTABLE NAMES ${_auroreengine_clang_scan_deps_names})
  if(NOT CLANG_SCAN_DEPS_EXECUTABLE)
    message(FATAL_ERROR "clang-scan-deps is required for C++23 module dependency scanning with Clang.")
  endif()
  set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${CLANG_SCAN_DEPS_EXECUTABLE}" CACHE FILEPATH "Path to clang-scan-deps" FORCE)
  add_compile_options(-Wno-nullability-completeness)
endif()

option(AUROREENGINE_BUILD_EXAMPLES "Build examples" OFF)
option(AUROREENGINE_BUILD_WITH_POSIX_THREADS "Build with POSIX threads" OFF)
option(AUROREENGINE_ALIAS_MODULES "CMake: Alias C++ module libraries" ON)
option(AUROREENGINE_FIND_PACKAGES "CMake: Find packages (if you disable this, you need to provide them)" ON)
option(AUROREENGINE_BUILD_V2_TESTS "Build backend-neutral Aurore v2 contract tests" ${PROJECT_IS_TOP_LEVEL})
option(AUROREENGINE_BUILD_RENDER_TESTS "Expose opt-in headless Vulkan rendering smoke targets" OFF)
option(AUROREENGINE_STABLE_CLANG_MODULE_OPTIMIZATION
  "Use the stable optimization tier for Clang C++ modules on Windows" ON)

if(AUROREENGINE_BUILD_WITH_POSIX_THREADS AND NOT WIN32)
  add_compile_options(-pthread)
endif()

include(FetchContent)

# The following targets are required (one way or another):
# - SDL2::SDL2
# - SDL2_image::SDL2_image
# - SDL2_mixer::SDL2_mixer
# - Freetype::Freetype
# - glm::glm
if(AUROREENGINE_FIND_PACKAGES)
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
  GIT_TAG        v1.4.325
  GIT_SUBMODULES_RECURSE FALSE
)
set(VULKAN_HPP_BUILD_WITH_LOCAL_VULKAN_HPP ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Vulkan-Hpp)

FetchContent_Declare(
  VulkanMemoryAllocator-Hpp
  GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
  GIT_TAG        73cdd838c425637c874d343ab0ceba5148189cbf
  GIT_SUBMODULES_RECURSE FALSE
  PATCH_COMMAND  ${CMAKE_COMMAND}
    -DVMA_HPP_SOURCE_DIR=<SOURCE_DIR>
    -DVULKAN_HPP_SOURCE_DIR=${vulkan-hpp_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchVmaHpp.cmake
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

if(AUROREENGINE_BUILD_V2_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(AUROREENGINE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(AUROREENGINE_BUILD_RENDER_TESTS)
  add_executable(aurore_textured_triangles_smoke EXCLUDE_FROM_ALL
    tests/integration/textured_triangles.cpp)
  target_compile_features(aurore_textured_triangles_smoke PRIVATE cxx_std_23)
  target_link_libraries(aurore_textured_triangles_smoke PRIVATE auroreengine::auroreengine)
endif()
