# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
set(SOURCES
  implementations.cpp
  phase.cpp
  resource_loader.cpp
  shaders.cpp
)
set(MODULE_SOURCES
  dreamrender.cppm

  debug.cppm
  gui_renderer.cppm
  input.cppm
  model.cppm
  phase.cppm
  resource_loader.cppm
  shaders.cppm
  texture.cppm
  utils.cppm
  window.cppm

  components/font_renderer.cppm
  components/image_renderer.cppm
  components/simple_renderer.cppm
)

# The v2 substrate deliberately remains an ordinary, backend-neutral C++
# library. It can be consumed and tested without Vulkan, SDL, modules, or
# generated shaders while the existing renderer is migrated incrementally.
add_library(aurore_core_v2 STATIC
  v2/core/time.cpp
)
add_library(Aurore::Core ALIAS aurore_core_v2)
target_compile_features(aurore_core_v2 PUBLIC cxx_std_23)
target_include_directories(aurore_core_v2
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(aurore_core_v2 PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_library(dreamrender ${SOURCES})
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
   DREAMS_STABLE_CLANG_MODULE_OPTIMIZATION)
  # Clang 22's Windows frontend intermittently crashes in the O3 inliner on
  # standard-library-heavy module interface units. The final application still
  # optimizes its consumers; keep engine BMI/object generation deterministic.
  target_compile_options(dreamrender PRIVATE $<$<CONFIG:Release>:-O0>)
endif()
target_sources(dreamrender PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  FILES ${MODULE_SOURCES}
)
target_link_libraries(dreamrender PUBLIC
  ${PROJECT_NAME}_glmModule
  ${PROJECT_NAME}_sdl2Module
  ${PROJECT_NAME}_spdlogModule
  ${PROJECT_NAME}_VulkanHppModule
  ${PROJECT_NAME}_vmaModule
)
target_link_libraries(dreamrender PUBLIC Freetype::Freetype)

add_dependencies(dreamrender ${PROJECT_NAME}_shaders)
target_compile_options(dreamrender PRIVATE --embed-dir=${CMAKE_CURRENT_BINARY_DIR}/../)

add_library(dreams::dreamrender ALIAS dreamrender)

# Optional HarfBuzz shaping support (robust across CONFIG and pkg-config installs)
set(DREAMRENDER_USE_HB OFF)

# Try CMake config package first
find_package(HarfBuzz CONFIG QUIET)
if(HarfBuzz_FOUND)
  if(TARGET HarfBuzz::harfbuzz)
    target_link_libraries(dreamrender PUBLIC HarfBuzz::harfbuzz)
    set(DREAMRENDER_USE_HB ON)
  elseif(TARGET harfbuzz::harfbuzz)
    target_link_libraries(dreamrender PUBLIC harfbuzz::harfbuzz)
    set(DREAMRENDER_USE_HB ON)
  elseif(DEFINED HarfBuzz_LIBRARIES)
    target_link_libraries(dreamrender PUBLIC ${HarfBuzz_LIBRARIES})
    if(DEFINED HarfBuzz_INCLUDE_DIRS)
      target_include_directories(dreamrender PUBLIC ${HarfBuzz_INCLUDE_DIRS})
    endif()
    set(DREAMRENDER_USE_HB ON)
  endif()
endif()

# Fallback to pkg-config if no CMake config with targets
if(NOT DREAMRENDER_USE_HB)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_HB QUIET harfbuzz)
    if(PC_HB_FOUND)
      target_include_directories(dreamrender PUBLIC ${PC_HB_INCLUDE_DIRS})
      target_link_libraries(dreamrender PUBLIC ${PC_HB_LINK_LIBRARIES} ${PC_HB_LIBRARIES})
      set(DREAMRENDER_USE_HB ON)
    endif()
  endif()
endif()

if(DREAMRENDER_USE_HB)
  target_compile_definitions(dreamrender PUBLIC DREAMRENDER_USE_HARFBUZZ=1)
  message(STATUS "dreamrender: HarfBuzz found — shaping enabled")
else()
  message(STATUS "dreamrender: HarfBuzz not found — shaping disabled")
endif()
