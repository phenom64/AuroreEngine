set(SOURCES 
  implementations.cpp
  phase.cpp
  resource_loader.cpp
)
set(MODULE_SOURCES
  dreamrender.cppm
  debug.cppm
  input.cppm
  model.cppm
  phase.cppm
  resource_loader.cppm
  texture.cppm
  utils.cppm
  window.cppm
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

add_library(dreams::dreamrender ALIAS dreamrender)
