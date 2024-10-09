add_custom_target(${PROJECT_NAME}_shaders)

include(../cmake/AddShader.cmake)
add_shader(${PROJECT_NAME}_shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.vert)
add_shader(${PROJECT_NAME}_shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.geom)
add_shader(${PROJECT_NAME}_shaders ${CMAKE_CURRENT_SOURCE_DIR}/font_renderer.frag)
add_shader(${PROJECT_NAME}_shaders ${CMAKE_CURRENT_SOURCE_DIR}/image_renderer.vert)
add_shader(${PROJECT_NAME}_shaders ${CMAKE_CURRENT_SOURCE_DIR}/image_renderer.frag)
