add_executable(runner runner.cpp)
target_include_directories(runner PRIVATE ${REPO_ROOT}/src)
target_compile_definitions(runner PRIVATE GAFFA_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(runner gaffa)

# Direct checks of builtin C functions, until running scripts works (see rtcheck.cpp)
add_executable(rtcheck rtcheck.cpp)
target_include_directories(rtcheck PRIVATE ${REPO_ROOT}/src)
target_link_libraries(rtcheck gaffa)
