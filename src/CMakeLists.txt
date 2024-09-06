set(src
    ../include/gaffa.h
    compiler-fixup.h
    gainternal.h
    lex.cpp
    lex.h
    parser.cpp
    parser.h
    defs.h
    defs.cpp
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
    symstore.cpp
    symstore.h
    mlir.cpp
    mlir.h
    gaimpdbg.cpp
    gaimpdbg.h
    hashfunc.cpp
    hashfunc.h
)

add_library(gaffa ${src})
add_executable(main main.cpp)
target_link_libraries(main gaffa)
