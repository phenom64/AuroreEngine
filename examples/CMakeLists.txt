add_executable(simple simple.cpp)
target_compile_features(simple PRIVATE cxx_std_23)
target_link_libraries(simple PRIVATE dreams::dreamrender)
