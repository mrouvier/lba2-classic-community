# Dynamic lighting's arithmetic (SOURCES/DYNLIGHT.H): the pool and lamp finders over a
# cube's baked light, the ramp-keeping surface table and the falloff. Pure
# inline helpers over plain values, so nothing here boots the engine.
add_executable(test_dynlight test_dynlight.cpp)

target_include_directories(test_dynlight PRIVATE
    ${CMAKE_SOURCE_DIR}/SOURCES
    ${CMAKE_SOURCE_DIR}/LIB386/H
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)

set_target_properties(test_dynlight PROPERTIES CXX_STANDARD 11)

add_test(NAME test_dynlight COMMAND test_dynlight)

register_host_test(test_dynlight)
