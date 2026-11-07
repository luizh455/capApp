# CompilerWarnings.cmake - Setup warnings according to repository guidelines
# - 4-space indentation
# - Enable compiler warnings: -Wall -Wextra -Wpedantic (or MSVC equivalent /W4)

function(target_set_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /volatile:iso
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
        )
    endif()
endfunction()
