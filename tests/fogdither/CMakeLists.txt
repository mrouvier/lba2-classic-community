# The fog dither's band pick (LIB386/H/POLYGON/POLYTZF.H): pure arithmetic in a
# header, with no ASM counterpart, so it belongs to the host suite. The
# equivalence suite covers the fillers that call it with the dither off.
add_executable(test_fog_dither test_fog_dither.cpp)

target_include_directories(test_fog_dither PRIVATE
    ${CMAKE_SOURCE_DIR}/LIB386/H
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)

set_target_properties(test_fog_dither PROPERTIES CXX_STANDARD 11)

add_test(NAME test_fog_dither COMMAND test_fog_dither)

register_host_test(test_fog_dither)

# The fillers themselves, on this host's C++ build of them: where the dither
# lands in the frame, and the water redraw's filter against its Z-buffered twin.
add_executable(test_fog_fillers test_fog_fillers.cpp)

target_include_directories(test_fog_fillers PRIVATE
    ${CMAKE_SOURCE_DIR}/LIB386/H
    ${CMAKE_SOURCE_DIR}/tests           # test_harness.h
    ${CMAKE_SOURCE_DIR}/tests/pol_work  # poly_test_fixture.h
)

target_link_libraries(test_fog_fillers PRIVATE poly svg sys lba2_platform)

set_target_properties(test_fog_fillers PROPERTIES CXX_STANDARD 11)

add_test(NAME test_fog_fillers COMMAND test_fog_fillers)

register_host_test(test_fog_fillers)
