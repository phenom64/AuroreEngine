# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
add_library(${PROJECT_NAME}_VulkanHppModule STATIC)
target_sources(${PROJECT_NAME}_VulkanHppModule PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${vulkan-hpp_SOURCE_DIR}/vulkan
  FILES ${vulkan-hpp_SOURCE_DIR}/vulkan/vulkan.cppm
)
target_include_directories(${PROJECT_NAME}_VulkanHppModule BEFORE PUBLIC
  ${vulkan-hpp_SOURCE_DIR}
)
if(EXISTS "${vulkan-hpp_SOURCE_DIR}/Vulkan-Headers/include")
  target_include_directories(${PROJECT_NAME}_VulkanHppModule BEFORE PUBLIC
    ${vulkan-hpp_SOURCE_DIR}/Vulkan-Headers/include
  )
else()
  target_include_directories(${PROJECT_NAME}_VulkanHppModule PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()
target_compile_definitions(${PROJECT_NAME}_VulkanHppModule PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_options(${PROJECT_NAME}_VulkanHppModule PUBLIC
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wno-deprecated-declarations>)

add_library(${PROJECT_NAME}_spdlogModule STATIC)
target_sources(${PROJECT_NAME}_spdlogModule PUBLIC FILE_SET CXX_MODULES FILES spdlog.cppm)
target_link_libraries(${PROJECT_NAME}_spdlogModule PUBLIC spdlog::spdlog)

if(NOT TARGET SDL2::SDL2)
  find_package(SDL2 REQUIRED)
endif()
if(NOT TARGET SDL2_image::SDL2_image)
  find_package(SDL2_image REQUIRED)
endif()
if(NOT TARGET SDL2_mixer::SDL2_mixer)
  find_package(SDL2_mixer REQUIRED)
endif()

add_library(${PROJECT_NAME}_sdl2Module STATIC)
target_sources(${PROJECT_NAME}_sdl2Module PUBLIC FILE_SET CXX_MODULES FILES sdl2.cppm)
target_link_libraries(${PROJECT_NAME}_sdl2Module PUBLIC SDL2::SDL2 SDL2_mixer::SDL2_mixer SDL2_image::SDL2_image)

set(sdl_module_include_dirs)
foreach(sdl_target IN ITEMS SDL2::SDL2 SDL2_image::SDL2_image SDL2_mixer::SDL2_mixer)
  get_target_property(sdl_target_include_dirs ${sdl_target} INTERFACE_INCLUDE_DIRECTORIES)
  list(APPEND sdl_module_include_dirs ${sdl_target_include_dirs})
  foreach(sdl_include_dir IN LISTS sdl_target_include_dirs)
    get_filename_component(sdl_include_dir_name "${sdl_include_dir}" NAME)
    if(sdl_include_dir_name STREQUAL "SDL2")
      get_filename_component(sdl_include_parent_dir "${sdl_include_dir}" DIRECTORY)
      list(APPEND sdl_module_include_dirs "${sdl_include_parent_dir}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES sdl_module_include_dirs)
target_include_directories(${PROJECT_NAME}_sdl2Module PRIVATE ${sdl_module_include_dirs})

set(sdl_include_dirs)
foreach(sdl_include_dir IN LISTS sdl_module_include_dirs)
  if(EXISTS "${sdl_include_dir}/SDL_hints.h")
    list(APPEND sdl_include_dirs "${sdl_include_dir}")
  endif()
endforeach()

add_library(${PROJECT_NAME}_glmModule STATIC)
target_sources(${PROJECT_NAME}_glmModule PUBLIC FILE_SET CXX_MODULES FILES glm.cppm)
target_link_libraries(${PROJECT_NAME}_glmModule PUBLIC glm::glm)

add_library(${PROJECT_NAME}_vmaModule STATIC)
target_sources(${PROJECT_NAME}_vmaModule PUBLIC FILE_SET CXX_MODULES FILES vma.cppm)
target_link_libraries(${PROJECT_NAME}_vmaModule PUBLIC ${PROJECT_NAME}_VulkanHppModule VulkanMemoryAllocator-Hpp)

if(DREAMS_ALIAS_MODULES)
  add_library(dreams::VulkanHppModule ALIAS ${PROJECT_NAME}_VulkanHppModule)
  add_library(dreams::spdlogModule ALIAS ${PROJECT_NAME}_spdlogModule)
  add_library(dreams::sdl2Module ALIAS ${PROJECT_NAME}_sdl2Module)
  add_library(dreams::glmModule ALIAS ${PROJECT_NAME}_glmModule)
  add_library(dreams::vmaModule ALIAS ${PROJECT_NAME}_vmaModule)
endif()

find_file(sdl_hints_h SDL_hints.h PATHS ${sdl_include_dirs})
if(NOT sdl_hints_h)
  message(FATAL_ERROR "SDL_hints.h was not found in SDL include directories")
endif()
set(sdl2_hints_inc "${CMAKE_CURRENT_BINARY_DIR}/codegen/generated/sdl2_hints.inc")
add_custom_command(
  OUTPUT "${sdl2_hints_inc}"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${sdl_hints_h}
    -DOUTPUT=${sdl2_hints_inc}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/codegen-sdl2_hints.cmake"
  DEPENDS "${sdl_hints_h}" "${CMAKE_CURRENT_SOURCE_DIR}/codegen-sdl2_hints.cmake"
  VERBATIM
)
target_sources(${PROJECT_NAME}_sdl2Module PRIVATE "${sdl2_hints_inc}")
target_include_directories(${PROJECT_NAME}_sdl2Module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/codegen)
