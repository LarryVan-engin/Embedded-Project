# Pre-build script: runs RASC to regenerate project files from configuration.xml
#
# Usage:
#   cmake -P cmake/prebuild.cmake

set(SCRIPT_DIR ${CMAKE_SCRIPT_MODE_FILE}/..)
set(RASC_CONFIG_FILE ${SCRIPT_DIR}/../configuration.xml)

include(${SCRIPT_DIR}/../Config.cmake)

execute_process(
    COMMAND ${RASC_EXE_PATH}
        -nosplash --launcher.suppressErrors
        --generate --devicefamily ra
        --compiler GCC
        --toolchainversion ${CMAKE_C_COMPILER_VERSION}
        ${RASC_CONFIG_FILE}
    RESULT_VARIABLE RASC_EXIT_CODE
)

if(NOT RASC_EXIT_CODE EQUAL "0")
    message(FATAL_ERROR "RASC generate command failed (exit code ${RASC_EXIT_CODE})")
endif()
