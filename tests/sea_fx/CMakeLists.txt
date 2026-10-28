# The moving sea's arithmetic (SOURCES/SEA_FX.H): the current's offset, the
# shifted corners, the chop's phase and mix. Pure inline helpers over plain
# values, so nothing here boots the engine.
add_executable(test_sea_fx test_sea_fx.cpp)

target_include_directories(test_sea_fx PRIVATE
    ${CMAKE_SOURCE_DIR}/SOURCES
    ${CMAKE_SOURCE_DIR}/LIB386/H
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)

set_target_properties(test_sea_fx PROPERTIES CXX_STANDARD 11)

add_test(NAME test_sea_fx COMMAND test_sea_fx)

register_host_test(test_sea_fx)
