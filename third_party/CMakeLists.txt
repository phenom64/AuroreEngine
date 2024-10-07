add_library(VulkanHppModule STATIC)
target_sources(VulkanHppModule PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS ${Vulkan_INCLUDE_DIR}
  FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)
target_link_libraries(VulkanHppModule PUBLIC Vulkan::Headers)
target_compile_definitions(VulkanHppModule PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_options(VulkanHppModule PUBLIC -Wno-deprecated-declarations)

add_library(spdlogModule STATIC)
target_sources(spdlogModule PUBLIC FILE_SET CXX_MODULES FILES spdlog.cppm)
target_link_libraries(spdlogModule PUBLIC spdlog::spdlog)

add_library(sdl2Module STATIC)
target_sources(sdl2Module PUBLIC FILE_SET CXX_MODULES FILES sdl2.cppm)
target_link_libraries(sdl2Module PUBLIC SDL2::SDL2 SDL2_mixer::SDL2_mixer SDL2_image::SDL2_image)

add_library(glmModule STATIC)
target_sources(glmModule PUBLIC FILE_SET CXX_MODULES FILES glm.cppm)
target_include_directories(glmModule PRIVATE ${GLM_INCLUDE_DIRS})

add_library(vmaModule STATIC)
target_sources(vmaModule PUBLIC FILE_SET CXX_MODULES FILES vma.cppm)
target_link_libraries(vmaModule PUBLIC VulkanHppModule VulkanMemoryAllocator-Hpp)
