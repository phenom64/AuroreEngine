# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
find_package(Vulkan REQUIRED COMPONENTS glslangValidator)

if(DEFINED Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    set(GLSLang_Validator "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
elseif(TARGET Vulkan::glslangValidator)
    set(GLSLang_Validator Vulkan::glslangValidator)
else()
    find_program(GLSLang_Validator glslangValidator REQUIRED)
endif()
set(DREAMS_GLSLANG_VALIDATOR "${GLSLang_Validator}" CACHE STRING "glslangValidator command used by dreams_add_shader" FORCE)

function(${PROJECT_NAME}_add_shader TARGET SHADER)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv)
    get_filename_component(output_dir "${output}" DIRECTORY)

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND ${DREAMS_GLSLANG_VALIDATOR} -V -o "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        VERBATIM)

    set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${output})
endfunction(${PROJECT_NAME}_add_shader)
