# ============================================================================
# 编译警告配置
#
# 抽取为单独函数，便于在多目标间复用。遵循 Cubium 规范：把警告当作
# 错误处理，尽早暴露问题（与"不要过度防御性编程"理念一致）。
# ============================================================================
function(set_project_warnings target_name)
    set(CLANG_GCC_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast          # 禁止 C 风格强制转换（规范 2.3）
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Werror
    )

    set(MSVC_WARNINGS
        /W4
        /permissive-              # 严格标准
        /WX                       # 警告视为错误
        /w14242                   # conversion, possible loss of data
        /w14254                   # operator conversion, possible loss of data
        /w14263                   # member function not overridden
        /w14265                   # class has virtual functions, destructor not virtual
        /w14287                   # unsigned/signed constant mismatch
        /w14296                   # expression is always false
        /w14311                   # pointer truncation
        /w14545                   # expression before comma evaluates to a function
        /w14546                   # function call before comma missing argument list
        /w14547                   # operator before comma has no effect
        /w14549                   # operator before comma has no effect
        /w14555                   # expression has no effect
        /w14619                   # pragma warning: there is no warning number
        /w14640                   # thread unsafe static member
        /w14826                   # conversion is sign-extended
        /w14905                   # wide string literal cast to LPSTR
        /w14906                   # string literal cast to LPWSTR
        /w14928                   # illegal copy-initialization
    )

    if(MSVC)
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    else()
        set(PROJECT_WARNINGS ${CLANG_GCC_WARNINGS})
    endif()

    target_compile_options(${target_name} INTERFACE ${PROJECT_WARNINGS})
endfunction()
