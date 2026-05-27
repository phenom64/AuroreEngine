if(NOT DEFINED VMA_HPP_SOURCE_DIR)
  message(FATAL_ERROR "VMA_HPP_SOURCE_DIR is required")
endif()
if(NOT DEFINED VULKAN_HPP_SOURCE_DIR)
  message(FATAL_ERROR "VULKAN_HPP_SOURCE_DIR is required")
endif()

set(_vma_cmake "${VMA_HPP_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_vma_cmake}")
  message(FATAL_ERROR "VulkanMemoryAllocator-Hpp CMakeLists.txt not found at ${_vma_cmake}")
endif()

file(READ "${_vma_cmake}" _content)

if(NOT _content MATCHES "Vulkan-Headers/include")
  string(REPLACE
    "$<BUILD_INTERFACE:\${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:\${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${VULKAN_HPP_SOURCE_DIR}>
        $<BUILD_INTERFACE:${VULKAN_HPP_SOURCE_DIR}/Vulkan-Headers/include>"
    _content "${_content}")
endif()

string(REPLACE
  "Vulkan::Vulkan
        VulkanMemoryAllocator::VulkanMemoryAllocator"
  "VulkanMemoryAllocator"
  _content "${_content}")

if(NOT _content MATCHES "TARGETS VulkanMemoryAllocator-Hpp VulkanMemoryAllocator")
  string(REPLACE
    "TARGETS VulkanMemoryAllocator-Hpp"
    "TARGETS VulkanMemoryAllocator-Hpp VulkanMemoryAllocator"
    _content "${_content}")
endif()
string(REPLACE
  "TARGETS VulkanMemoryAllocator-Hpp VulkanMemoryAllocator VulkanMemoryAllocator"
  "TARGETS VulkanMemoryAllocator-Hpp VulkanMemoryAllocator"
  _content "${_content}")

string(REPLACE [=[
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/vk_mem_alloc_enums.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/vk_mem_alloc_funcs.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/vk_mem_alloc_handles.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/vk_mem_alloc_structs.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/vk_mem_alloc.hpp
    DESTINATION
        include
)
]=] "" _content "${_content}")

string(REPLACE [=[
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanMemoryAllocator-HppConfig.cmake
    DESTINATION
        lib/cmake/VulkanMemoryAllocator-Hpp
)
]=] "" _content "${_content}")

string(REPLACE [=[
install(
    TARGETS VulkanMemoryAllocator-Hpp VulkanMemoryAllocator
    EXPORT VulkanMemoryAllocator-HppTargets
)
]=] "" _content "${_content}")

string(REPLACE [=[
install(
    EXPORT VulkanMemoryAllocator-HppTargets
    FILE VulkanMemoryAllocator-HppTargets.cmake
    NAMESPACE VulkanMemoryAllocator-Hpp::
    DESTINATION lib/cmake/VulkanMemoryAllocator-Hpp
)
]=] "" _content "${_content}")

file(WRITE "${_vma_cmake}" "${_content}")
