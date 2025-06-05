add_library(${PROJECT_NAME}_VulkanHppModule STATIC)
target_sources(${PROJECT_NAME}_VulkanHppModule PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${Vulkan_INCLUDE_DIR}
  FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)
vulkan_hpp__setup_vulkan_include(NAME ${PROJECT_NAME}_VulkanHppModule)
target_compile_definitions(${PROJECT_NAME}_VulkanHppModule PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_options(${PROJECT_NAME}_VulkanHppModule PUBLIC -Wno-deprecated-declarations)

add_library(${PROJECT_NAME}_spdlogModule STATIC)
target_sources(${PROJECT_NAME}_spdlogModule PUBLIC FILE_SET CXX_MODULES FILES spdlog.cppm)
target_link_libraries(${PROJECT_NAME}_spdlogModule PUBLIC spdlog::spdlog)

add_library(${PROJECT_NAME}_sdl2Module STATIC)
target_sources(${PROJECT_NAME}_sdl2Module PUBLIC FILE_SET CXX_MODULES FILES sdl2.cppm)
target_link_libraries(${PROJECT_NAME}_sdl2Module PUBLIC SDL2::SDL2 SDL2_mixer::SDL2_mixer SDL2_image::SDL2_image)

add_library(${PROJECT_NAME}_glmModule STATIC)
target_sources(${PROJECT_NAME}_glmModule PUBLIC FILE_SET CXX_MODULES FILES glm.cppm)
target_include_directories(${PROJECT_NAME}_glmModule PRIVATE glm::glm)

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

get_target_property(sdl_include_dirs SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
find_file(sdl_hints_h SDL_hints.h PATHS ${sdl_include_dirs})
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/codegen/generated/sdl2_hints.inc
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/codegen-sdl2_hints.sh
    ${sdl_hints_h} ${CMAKE_CURRENT_BINARY_DIR}/codegen/generated/sdl2_hints.inc
)
target_sources(${PROJECT_NAME}_sdl2Module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/codegen/generated/sdl2_hints.inc)
target_include_directories(${PROJECT_NAME}_sdl2Module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/codegen)
