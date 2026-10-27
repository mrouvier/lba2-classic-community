# The storm's scale arithmetic (SOURCES/RAIN_SCALE.H): the drop count, box spans and
# streak width RAIN.CPP runs for a framebuffer, and the pool, which RAIN.CPP holds equal
# to COMMON.H's MAX_RAIN at compile time. Header-only, like the settings tests.
add_executable(test_rain_scale test_rain_scale.cpp)

target_include_directories(test_rain_scale PRIVATE
    ${CMAKE_SOURCE_DIR}/SOURCES  # RAIN_SCALE.H, RES_CATALOG.H
    ${CMAKE_SOURCE_DIR}/LIB386/H # SYSTEM/ADELINE_TYPES.H (S32), SYSTEM/LIMITS.H
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)
set_target_properties(test_rain_scale PROPERTIES CXX_STANDARD 11)

add_test(NAME test_rain_scale COMMAND test_rain_scale)
register_host_test(test_rain_scale)

# The storm look's arithmetic (SOURCES/RAIN_FX.H): the wind, the drift and the blend strength
# by depth. Header-only as well.
add_executable(test_rain_fx test_rain_fx.cpp)

target_include_directories(test_rain_fx PRIVATE
    ${CMAKE_SOURCE_DIR}/SOURCES  # RAIN_FX.H
    ${CMAKE_SOURCE_DIR}/LIB386/H # SYSTEM/ADELINE_TYPES.H (S32)
    ${CMAKE_SOURCE_DIR}/tests    # test_harness.h
)
set_target_properties(test_rain_fx PROPERTIES CXX_STANDARD 11)

add_test(NAME test_rain_fx COMMAND test_rain_fx)
register_host_test(test_rain_fx)
