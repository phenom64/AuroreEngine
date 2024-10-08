add_custom_target(shaders)

include(../cmake/AddShader.cmake)
add_shader(shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.vert)
add_shader(shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.geom)
add_shader(shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.frag)
add_shader(shaders ${CMAKE_CURRENT_SOURCE_DIR}/image_renderer.vert)
add_shader(shaders ${CMAKE_CURRENT_SOURCE_DIR}/image_renderer.frag)
