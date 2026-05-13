# =============================================================================
# GeneratedSrc.cmake — Build target definition for TESTING_2
#
# Source groups:
#   Source_Files      : Project C/C++ sources (src/, Driver/, BSP/)
#   TFLite_Source_Files : TFLite Micro core files, compiled with -w to silence
#                         third-party warnings
#   Kernel_C_Files    : Bare-metal RTOS kernel (C)
#   Kernel_ASM_Files  : Cortex-M33 context-switch port (Assembly)
#
# TFLite filter strategy:
#   GLOB_RECURSE picks up every .c/.cc in the TFLite tree.
#   We then explicitly exclude directories that are PC-only or require
#   platform-specific SDKs not available on Cortex-M33:
#     _test / benchmark   — unit tests requiring GTest
#     third_party         — Ruy/Gemmlowp sources use std::thread (no bare-metal)
#     examples / tools    — host-side utilities
#     testing / testdata  — host-side helpers
#     test_data_generation — standalone PC programs with their own main()
#     integration_tests   — Python resolver dependency
#     experimental        — KissFFT (audio) not needed
#     signal              — audio DSP ops, depends on KissFFT
#     python              — Python.h / pybind11 dependency
#     arc_* / xtensa etc  — platform-specific HAL for non-ARM cores
# =============================================================================

# ---------------------------------------------------------------------------
# Project sources (warnings enabled — these are our own files)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE Source_Files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Driver/Source/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/FWUpdate/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/**/*.c
)

# ---------------------------------------------------------------------------
# TFLite Micro sources (compiled with -w to suppress third-party warnings)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE TFLite_Source_Files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/**/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/**/*.cc
)

# --- Exclude PC-only and platform-specific files ---
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*_test\\.cc$")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*_test\\.c$")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*benchmark.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*third_party.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*examples.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*testing.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*testdata.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*test_data_generation.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*integration_tests.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*experimental.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*tools.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*signal.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*python.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*(arc_custom|arc_emsdp|arc_mli|bluepill|ceva|chre|cortex_m_corstone_300|cortex_m_generic|ethos_u|hexagon|riscv32_generic|xtensa|cmsis_nn|models).*")

# ---------------------------------------------------------------------------
# RTOS Kernel — C implementation and Cortex-M33 assembly port
# ---------------------------------------------------------------------------
file(GLOB_RECURSE Kernel_C_Files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/Kernel/src/*.c
)
file(GLOB_RECURSE Kernel_ASM_Files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/Kernel/port/*.S
)

# ---------------------------------------------------------------------------
# Executable target
# ---------------------------------------------------------------------------
set(ALL_FILES ${Source_Files} ${Kernel_C_Files} ${Kernel_ASM_Files})

add_executable(${PROJECT_NAME}.elf
    ${ALL_FILES}
    ${TFLite_Source_Files}
)

# Disable all warnings from TFLite sources (third-party, do not modify)
foreach(tflite_src ${TFLite_Source_Files})
    set_source_files_properties(${tflite_src} PROPERTIES COMPILE_FLAGS "-w")
endforeach()

# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
target_compile_options(${PROJECT_NAME}.elf
    PRIVATE
    $<$<CONFIG:Debug>:${RASC_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RASC_RELEASE_FLAGS}>
    $<$<CONFIG:MinSizeRel>:${RASC_MIN_SIZE_RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${RASC_RELEASE_WITH_DEBUG_INFO}>
)

target_compile_options(${PROJECT_NAME}.elf PRIVATE $<$<COMPILE_LANGUAGE:C>:${RASC_CMAKE_C_FLAGS}>)
target_compile_options(${PROJECT_NAME}.elf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${RASC_CMAKE_CXX_FLAGS}>)

target_link_options(${PROJECT_NAME}.elf PRIVATE $<$<LINK_LANGUAGE:C>:${RASC_CMAKE_EXE_LINKER_FLAGS}>)
target_link_options(${PROJECT_NAME}.elf PRIVATE $<$<LINK_LANGUAGE:CXX>:${RASC_CMAKE_EXE_LINKER_FLAGS}>)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${RASC_CMAKE_DEFINITIONS})

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME}.elf
    PRIVATE
    # CMSIS headers for Cortex-M33 core registers
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/arm/CMSIS_6/CMSIS/Core/Include
    # Project headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test
    ${CMAKE_CURRENT_SOURCE_DIR}/Driver/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/FWUpdate
    # RTOS kernel headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/Kernel/include
    # BSP sensor drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/HS3001
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/ZMOD4410
    # TFLite Micro — root (for tensorflow/lite/... includes)
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite
    # TFLite third-party headers (FlatBuffers, Gemmlowp — headers only, not compiled)
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party/flatbuffers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party/gemmlowp
    # Project root (for top-level headers if any)
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/
)

# ---------------------------------------------------------------------------
# Linker
# ---------------------------------------------------------------------------
target_link_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/script
)

target_link_libraries(${PROJECT_NAME}.elf
    PRIVATE
)

# ---------------------------------------------------------------------------
# Post-build: generate Motorola S-record for J-Link flashing
# ---------------------------------------------------------------------------
add_custom_command(
    TARGET ${PROJECT_NAME}.elf
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O srec ${PROJECT_NAME}.elf ${PROJECT_NAME}.srec
    COMMENT "Generating S-record: ${PROJECT_NAME}.srec"
)
