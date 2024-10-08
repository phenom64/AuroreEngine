set(EXAMPLES
  simple
  font_renderer
)

foreach(EXAMPLE ${EXAMPLES})
  add_executable(${EXAMPLE} ${EXAMPLE}.cpp)
  target_compile_features(${EXAMPLE} PRIVATE cxx_std_23)
  target_link_libraries(${EXAMPLE} PRIVATE dreams::dreamrender)
endforeach()
