add_library(
    passes STATIC
    DeadCode.cpp
    Dominators.cpp
    FuncInfo.cpp
    Mem2Reg.cpp
    FunctionInline.cpp
    )

target_link_libraries(passes common)