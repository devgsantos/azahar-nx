function(azahar_native_test_deferred)
    message(STATUS "Azahar native deferred overlay enabled")
endfunction()
cmake_language(DEFER CALL azahar_native_test_deferred)
