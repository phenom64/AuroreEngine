find_program(GLSLang_Validator glslangValidator REQUIRED)
function(add_shader TARGET SHADER)
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${SHADER})
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${rel}.spv)

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${GLSLang_Validator} -V -o ${output} ${SHADER}
        DEPENDS ${SHADER}
        VERBATIM)

    set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${output})
endfunction(add_shader)
