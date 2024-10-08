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
)

add_library(dreamrender ${SOURCES})
target_sources(dreamrender PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  FILES ${MODULE_SOURCES}
)
target_link_libraries(dreamrender PUBLIC
  glmModule
  sdl2Module
  spdlogModule
  VulkanHppModule
  vmaModule
)
target_link_libraries(dreamrender PUBLIC Freetype::Freetype)

add_dependencies(dreamrender shaders)
target_compile_options(dreamrender PRIVATE --embed-dir=${CMAKE_CURRENT_BINARY_DIR}/../shaders)
target_include_directories(dreamrender PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/../shaders)

add_library(dreams::dreamrender ALIAS dreamrender)
