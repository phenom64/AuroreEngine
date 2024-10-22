set(EXAMPLES
  simple
  font_renderer
  image_renderer
  gui_renderer
  transparency
)

foreach(EXAMPLE ${EXAMPLES})
  add_executable(${EXAMPLE} ${EXAMPLE}.cpp)
  target_compile_features(${EXAMPLE} PRIVATE cxx_std_23)
  target_link_libraries(${EXAMPLE} PRIVATE dreams::dreamrender)
  target_compile_options(${EXAMPLE} PRIVATE --embed-dir=${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

target_link_libraries(transparency PRIVATE X11)
