cmake_minimum_required (VERSION 2.8.11)
project(libperfs "C")

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

add_library(dyndbg SHARED "")
add_library(dyndbg_static STATIC "")
set_target_properties(dyndbg_static PROPERTIES OUTPUT_NAME dyndbg)

target_sources(dyndbg
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/dyndbg_us.c
        ${CMAKE_CURRENT_LIST_DIR}/src/dyndbg_monitor.c
        ${CMAKE_CURRENT_LIST_DIR}/include/private/dyndbg_monitor.h
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include/dyndbg/dyndbg_us.h
)
target_sources(dyndbg_static
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/dyndbg_us.c
        ${CMAKE_CURRENT_LIST_DIR}/src/dyndbg_monitor.c
        ${CMAKE_CURRENT_LIST_DIR}/include/private/dyndbg_monitor.h
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include/dyndbg/dyndbg_us.h
)

add_executable(unit_test ${CMAKE_CURRENT_LIST_DIR}/tests/test.c)
target_link_libraries(unit_test dyndbg)

add_executable(unit_test_static ${CMAKE_CURRENT_LIST_DIR}/tests/test.c)
target_link_libraries(unit_test_static dyndbg_static)
