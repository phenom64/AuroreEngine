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

add_library(dreamrender ${SOURCES})
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
