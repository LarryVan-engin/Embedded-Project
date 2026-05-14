# Compiler and linker flags for RA6M5 (Cortex-M33, R7FA6M5BH)

set(RASC_TARGET_DEVICE R7FA6M5BH)
set(RASC_TARGET_ARCH cortex-m33)
set(RASC_PROJECT_NAME TESTING_2)
set(RASC_TOOLCHAIN_NAME GCC)

set(RASC_CMAKE_ASM_FLAGS
    "-mfloat-abi=hard;-mcpu=cortex-m33;-mfpu=fpv5-sp-d16"
    "-Wunused;-Wuninitialized;-Wall;-Wextra;-Wmissing-declarations"
    "-Wconversion;-Wpointer-arith;-Wshadow;-Wlogical-op;-Waggregate-return;-Wfloat-equal"
    "-fmessage-length=0;-fsigned-char;-ffunction-sections;-fdata-sections"
    "-mthumb;-x;assembler-with-cpp;-MMD;-MP"
)
string(REPLACE ";" ";" RASC_CMAKE_ASM_FLAGS "${RASC_CMAKE_ASM_FLAGS}")

set(RASC_CMAKE_C_FLAGS
    -mfloat-abi=hard
    -mcpu=cortex-m33
    -mfpu=fpv5-sp-d16
    -Wunused
    -Wuninitialized
    -Wall
    -Wextra
    -Wmissing-declarations
    -Wconversion
    -Wpointer-arith
    -Wshadow
    -Wlogical-op
    -Waggregate-return
    -Wfloat-equal
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -mthumb
    -std=c99
    -MMD
    -MP
)

set(RASC_CMAKE_CXX_FLAGS
    -mfloat-abi=hard
    -mcpu=cortex-m33
    -mfpu=fpv5-sp-d16
    -Wunused
    -Wuninitialized
    -Wall
    -Wextra
    -Wmissing-declarations
    -Wconversion
    -Wpointer-arith
    -Wshadow
    -Wlogical-op
    -Wno-aggregate-return    # TFLite Micro Add*() ops return aggregates by design
    -Wfloat-equal
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -mthumb
    -std=c++14
    -fno-rtti
    -fno-exceptions
    -fno-threadsafe-statics
    -MMD
    -MP
)

set(RASC_CMAKE_EXE_LINKER_FLAGS
    -mfloat-abi=hard
    -mcpu=cortex-m33
    -mfpu=fpv5-sp-d16
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -mthumb
    -T
    script/fsp.ld
    -Wl,--gc-sections
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map"
    --specs=nano.specs
    --specs=nosys.specs
    -o
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf"
)

set(RASC_CMAKE_DEFINITIONS "_RA_CORE=CM33;_RA_ORDINAL=1;_RENESAS_RA_")
set(RASC_ASM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/*.asm")

# GCC >= 12.2 extra flags
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.2)
    list(INSERT RASC_CMAKE_C_FLAGS 0 --param=min-pagesize=0 -Wno-format-truncation -Wno-stringop-overflow)
    list(INSERT RASC_CMAKE_CXX_FLAGS 0 --param=min-pagesize=0 -Wno-format-truncation -Wno-stringop-overflow)
endif()

set(RASC_DEBUG_FLAGS           -g;-O0)
set(RASC_RELEASE_FLAGS         -O2)
set(RASC_MIN_SIZE_RELEASE_FLAGS -Os)
set(RASC_RELEASE_WITH_DEBUG_INFO -g;-O2)

include_guard()

# Platform-aware command to open RASC
file(TO_NATIVE_PATH "${RASC_EXE_PATH}" RASC_EXE_NATIVE_PATH)
if(CMAKE_HOST_WIN32)
    set(RASC_COMMAND start "" /b "${RASC_EXE_NATIVE_PATH}" configuration.xml)
else()
    set(RASC_COMMAND sh -c "\"${RASC_EXE_NATIVE_PATH} configuration.xml &\"")
endif()

add_custom_target(open_rasc_${PROJECT_NAME}
    COMMAND ${RASC_COMMAND}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Opening FSP Smart Configurator"
)
