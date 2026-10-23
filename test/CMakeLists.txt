add_executable(runner runner.cpp)
target_include_directories(runner PRIVATE ${REPO_ROOT}/src)
target_compile_definitions(runner PRIVATE GAFFA_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(runner gaffa)
