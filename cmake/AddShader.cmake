find_program(GLSLang_Validator glslangValidator REQUIRED)
function(${PROJECT_NAME}_add_shader TARGET SHADER)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${GLSLang_Validator} -V -o ${output} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        VERBATIM)

    set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${output})
endfunction(${PROJECT_NAME}_add_shader)
