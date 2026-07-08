function(azahar_add_native_runtime_support)
    if(TARGET azahar_switch)
        target_sources(azahar_switch PRIVATE
            "${CMAKE_SOURCE_DIR}/src/switch/switch_native_runtime.cpp")
    endif()
endfunction()
cmake_language(DEFER CALL azahar_add_native_runtime_support)
