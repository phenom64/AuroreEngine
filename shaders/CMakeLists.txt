# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
add_custom_target(${PROJECT_NAME}_shaders)

include(../cmake/AddShader.cmake)
auroreengine_add_shader(${PROJECT_NAME}_shaders font_renderer.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders font_renderer.compat.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders font_renderer.geom)
auroreengine_add_shader(${PROJECT_NAME}_shaders font_renderer.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.compat.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.glass.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.triangles.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.triangles.batch.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.triangles.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders image_renderer.triangles.compat.frag)
auroreengine_add_shader(${PROJECT_NAME}_shaders simple_renderer.vert)
auroreengine_add_shader(${PROJECT_NAME}_shaders simple_renderer.frag)
