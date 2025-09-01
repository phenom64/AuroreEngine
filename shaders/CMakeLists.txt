# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
add_custom_target(${PROJECT_NAME}_shaders)

include(../cmake/AddShader.cmake)
dreams_add_shader(${PROJECT_NAME}_shaders font_renderer.vert)
dreams_add_shader(${PROJECT_NAME}_shaders font_renderer.compat.vert)
dreams_add_shader(${PROJECT_NAME}_shaders font_renderer.geom)
dreams_add_shader(${PROJECT_NAME}_shaders font_renderer.frag)
dreams_add_shader(${PROJECT_NAME}_shaders image_renderer.vert)
dreams_add_shader(${PROJECT_NAME}_shaders image_renderer.frag)
dreams_add_shader(${PROJECT_NAME}_shaders image_renderer.compat.frag)
dreams_add_shader(${PROJECT_NAME}_shaders simple_renderer.vert)
dreams_add_shader(${PROJECT_NAME}_shaders simple_renderer.frag)
