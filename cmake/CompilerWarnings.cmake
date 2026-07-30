# ============================================================================
# Compile warning configuration
#
# Extracted into a separate function for reuse across multiple targets. Follows
# the Cubium spec: treat warnings as errors to surface problems early (in line
# with the "avoid excessive defensive programming" philosophy).
# ============================================================================
function(set_project_warnings target_name)
    set(CLANG_GCC_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast          # forbid C-style casts (spec 2.3)
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
        /permissive-              # strict standards conformance
        /WX                       # treat warnings as errors
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
