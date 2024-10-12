add_library(${PROJECT_NAME}_VulkanHppModule STATIC)
target_sources(${PROJECT_NAME}_VulkanHppModule PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${Vulkan_INCLUDE_DIR}
  FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)
target_link_libraries(${PROJECT_NAME}_VulkanHppModule PUBLIC Vulkan::Headers)
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
