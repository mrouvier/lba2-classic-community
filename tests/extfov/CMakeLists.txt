# The exterior view lock's arithmetic (SOURCES/EXTFOV.H): the focal and near clip for a frame
# height, the view's size in 480-line pixels, and the classic camera's margins. Header-only,
# like the settings tests.
add_executable(test_extfov test_extfov.cpp)

target_include_directories(test_extfov PRIVATE
    ${CMAKE_SOURCE_DIR}/SOURCES  # EXTFOV.H
    ${CMAKE_SOURCE_DIR}/LIB386/H # SYSTEM/ADELINE_TYPES.H (S32)
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)
set_target_properties(test_extfov PROPERTIES CXX_STANDARD 11)

add_test(NAME test_extfov COMMAND test_extfov)
register_host_test(test_extfov)
