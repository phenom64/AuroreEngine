# Backend-neutral v2 contract tests intentionally use no third-party test
# framework so they remain buildable in recovery and bootstrap environments.

function(aurore_add_v2_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE Aurore::Core)
  target_compile_features(${target} PRIVATE cxx_std_23)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()

  add_test(NAME ${target} COMMAND ${target})
endfunction()

aurore_add_v2_test(aurore_v2_time_tests v2/core/time_tests.cpp)
aurore_add_v2_test(aurore_v2_draw_list_tests v2/ui/minimal_draw_list_tests.cpp)

add_executable(aurore_swapchain_policy_tests integration/swapchain_policy_tests.cpp)
target_link_libraries(aurore_swapchain_policy_tests PRIVATE Vulkan::Headers)
target_compile_features(aurore_swapchain_policy_tests PRIVATE cxx_std_23)
set_target_properties(aurore_swapchain_policy_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME aurore_swapchain_policy_tests COMMAND aurore_swapchain_policy_tests)

add_executable(aurore_image_upload_tests integration/image_upload_tests.cpp)
target_compile_features(aurore_image_upload_tests PRIVATE cxx_std_23)
set_target_properties(aurore_image_upload_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME aurore_image_upload_tests COMMAND aurore_image_upload_tests)

add_executable(aurore_font_staging_tests integration/font_staging_tests.cpp)
target_compile_features(aurore_font_staging_tests PRIVATE cxx_std_23)
set_target_properties(aurore_font_staging_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME aurore_font_staging_tests COMMAND aurore_font_staging_tests)

# Supply a real consumer's packaged font directory to exercise the CPU shaper.
# No GPU, synthetic font fixture, or module import is needed by the test itself.
set(AUROREENGINE_FONT_TEST_DATA_DIR "" CACHE PATH "Directory containing Play-Regular.ttf and fonts/NotoSans* for fallback tests")
if(AUROREENGINE_FONT_TEST_DATA_DIR)
  find_package(HarfBuzz CONFIG QUIET)
endif()
if(AUROREENGINE_FONT_TEST_DATA_DIR AND
   (TARGET harfbuzz::harfbuzz OR TARGET HarfBuzz::harfbuzz))
  add_executable(aurore_font_fallback_tests integration/font_fallback_tests.cpp)
  target_link_libraries(aurore_font_fallback_tests PRIVATE Freetype::Freetype)
  if(TARGET harfbuzz::harfbuzz)
    target_link_libraries(aurore_font_fallback_tests PRIVATE harfbuzz::harfbuzz)
  else()
    target_link_libraries(aurore_font_fallback_tests PRIVATE HarfBuzz::harfbuzz)
  endif()
  target_compile_features(aurore_font_fallback_tests PRIVATE cxx_std_23)
  set_target_properties(aurore_font_fallback_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  if(MSVC)
    target_compile_options(aurore_font_fallback_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(aurore_font_fallback_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME aurore_font_fallback_tests COMMAND aurore_font_fallback_tests "${AUROREENGINE_FONT_TEST_DATA_DIR}")
  if(WIN32 AND DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set_tests_properties(aurore_font_fallback_tests PROPERTIES ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
  endif()
endif()

add_executable(aurore_large_texture_upload_tests EXCLUDE_FROM_ALL integration/large_texture_upload.cpp)
target_link_libraries(aurore_large_texture_upload_tests PRIVATE auroreengine::auroreengine)

add_executable(aurore_offscreen_presentation_tests EXCLUDE_FROM_ALL integration/offscreen_presentation.cpp)
target_link_libraries(aurore_offscreen_presentation_tests PRIVATE auroreengine::auroreengine)

add_executable(aurore_retained_target_extent_tests EXCLUDE_FROM_ALL integration/retained_target_extent.cpp)
target_link_libraries(aurore_retained_target_extent_tests PRIVATE auroreengine::auroreengine)
