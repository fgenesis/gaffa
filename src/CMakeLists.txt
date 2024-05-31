set(src
    ../include/gaffa.h
    gainternal.h
    lex.cpp
    lex.h
    parser.cpp
    parser.h
    ast.cpp
    ast.h
    defs.h
    util.h
    util.cpp
    strings.cpp
    strings.h
    gc.cpp
    gc.h
    hlir.cpp
    hlir.h
    typing.cpp
    typing.h
    gavm.cpp
    gavm.h
    table.cpp
    table.h
    array.cpp
    array.h
    rtdata.cpp
)

add_library(gaffa ${src})
add_executable(main main.cpp)
target_link_libraries(main gaffa)
