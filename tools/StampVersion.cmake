# Bazarish project (c) 2026
set(kUntaggedVersion "dev")

execute_process(
    COMMAND git -C "${BAZARISH_SOURCE_DIR}" describe --tags --abbrev=0
    OUTPUT_VARIABLE tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE tagStatus)
execute_process(
    COMMAND git -C "${BAZARISH_SOURCE_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE commitStatus)

if(tagStatus STREQUAL "0" AND commitStatus STREQUAL "0")
    set(BAZARISH_APP_VERSION "${tag} (${commit})")
else()
    set(BAZARISH_APP_VERSION "${kUntaggedVersion}")
endif()

configure_file("${BAZARISH_SOURCE_DIR}/app/Version.hpp.in"
    "${BAZARISH_VERSION_HEADER}" @ONLY)
