# Bazarish project (c) 2026
set(kUntaggedVersion "dev")

function(bazarish_git_version sourceDir outName outCode)
    execute_process(
        COMMAND git -C "${sourceDir}" describe --tags --exact-match
        OUTPUT_VARIABLE tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE tagStatus)
    execute_process(
        COMMAND git -C "${sourceDir}" rev-parse --short HEAD
        OUTPUT_VARIABLE commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE commitStatus)
    execute_process(
        COMMAND git -C "${sourceDir}" rev-list --count HEAD
        OUTPUT_VARIABLE commits
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE commitsStatus)
    if(NOT commitStatus STREQUAL "0" OR NOT commitsStatus STREQUAL "0")
        message(FATAL_ERROR "the version is read from git: ${sourceDir} has to be a git checkout")
    endif()
    if(tagStatus STREQUAL "0")
        set(${outName} "${tag}" PARENT_SCOPE)
    else()
        set(${outName} "${kUntaggedVersion} (${commit})" PARENT_SCOPE)
    endif()
    set(${outCode} "${commits}" PARENT_SCOPE)
endfunction()
