message(STATUS "Alif Ensemble Platform")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TARGET)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Resolve the Alif Ensemble CMSIS Device Family Pack (DFP). A static tree --
# CMSIS device headers,
# GNU/AC6 linker templates, startup code, and driver sources (UART/GPIO/MHU
# via Alif_CMSIS's CMSIS-Driver implementations and drivers/) -- no
# code-generation step at configure time. Clone it yourself:
#   git clone https://github.com/alifsemi/alif_ensemble-cmsis-dfp
# and point ALIF_CMSIS_DFP_PATH at the checkout (env var or -D). See this
# repo's README.md Setup section.
if(NOT ALIF_CMSIS_DFP_PATH)
    if(DEFINED ENV{ALIF_CMSIS_DFP_PATH})
        set(ALIF_CMSIS_DFP_PATH "$ENV{ALIF_CMSIS_DFP_PATH}")
    endif()
endif()
if(NOT ALIF_CMSIS_DFP_PATH OR NOT IS_DIRECTORY "${ALIF_CMSIS_DFP_PATH}")
    message(FATAL_ERROR
        "ALIF_CMSIS_DFP_PATH not set or not a directory (got '${ALIF_CMSIS_DFP_PATH}'). "
        "Clone https://github.com/alifsemi/alif_ensemble-cmsis-dfp and export "
        "ALIF_CMSIS_DFP_PATH=/path/to/alif_ensemble-cmsis-dfp, or pass "
        "-DALIF_CMSIS_DFP_PATH=...")
endif()
if(NOT IS_DIRECTORY "${ALIF_CMSIS_DFP_PATH}/Device/soc")
    message(FATAL_ERROR
        "ALIF_CMSIS_DFP_PATH (${ALIF_CMSIS_DFP_PATH}) has no Device/soc/ "
        "subdirectory -- is this a valid alif_ensemble-cmsis-dfp checkout?")
endif()

# The DFP vendors Alif's own driver implementations (Alif_CMSIS/Source/
# Driver_USART.c, Driver_GPIO.c, ...) but not the *base* ARM CMSIS-Driver
# API headers they implement against (Driver_USART.h, Driver_GPIO.h,
# Driver_Common.h) -- those come from ARM's own CMSIS_6 package
# (https://github.com/ARM-software/CMSIS_6), a separate, standard,
# vendor-agnostic dependency. Clone it and point CMSIS_6_PATH at the
# checkout (env var or -D). See this repo's README.md Setup section.
if(NOT CMSIS_6_PATH)
    if(DEFINED ENV{CMSIS_6_PATH})
        set(CMSIS_6_PATH "$ENV{CMSIS_6_PATH}")
    endif()
endif()
if(NOT CMSIS_6_PATH OR NOT EXISTS "${CMSIS_6_PATH}/CMSIS/Driver/Include/Driver_USART.h")
    message(FATAL_ERROR
        "CMSIS_6_PATH not set or missing CMSIS/Driver/Include/Driver_USART.h "
        "(got '${CMSIS_6_PATH}'). Clone https://github.com/ARM-software/CMSIS_6 "
        "and export CMSIS_6_PATH=/path/to/CMSIS_6, or pass -DCMSIS_6_PATH=...")
endif()

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_LINKER arm-none-eabi-ld)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)

if(NOT CMAKE_C_COMPILER)
    message(FATAL_ERROR "arm-none-eabi-gcc not found on PATH. Install the ARM GNU Toolchain.")
endif()

set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# RTSS-HP and RTSS-HE are both Cortex-M55 (Armv8.1-M, Helium/MVE + FPU) --
# same core flags for both; only clock speed, memory map, and NPU config
# (both have an Ethos-U55, not relevant to capi's own build) differ, and
# those come from capi_alif_config_sdk() (linker script + startup + device
# headers), not from compiler flags. APSS (Cortex-A32) is a stub target --
# no working toolchain wiring yet, see capi-alif's module.yml notes.
if("${TARGET}" MATCHES "_apss$")
    message(FATAL_ERROR
        "TARGET=${TARGET} has no working toolchain yet (APSS boot goes "
        "through TF-A/Secure-Enclave handoff, out of scope for a bare-metal "
        "capi app). Use the board's _rtss_hp or _rtss_he target.")
endif()
set(ALIF_MCU_FLAGS "-mcpu=cortex-m55 -mthumb -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${ALIF_MCU_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C compiler flags" FORCE)
set(CMAKE_CXX_FLAGS "${ALIF_MCU_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C++ compiler flags" FORCE)
set(CMAKE_ASM_FLAGS "${ALIF_MCU_FLAGS} -x assembler-with-cpp" CACHE STRING "ASM compiler flags" FORCE)

set(CMAKE_C_FLAGS_DEBUG "-g3 -Og -DDEBUG" CACHE STRING "C compiler flags for Debug" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -Og -DDEBUG" CACHE STRING "C++ compiler flags for Debug" FORCE)
set(CMAKE_ASM_FLAGS_DEBUG "-g3" CACHE STRING "ASM compiler flags for Debug" FORCE)

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C compiler flags for Release" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C++ compiler flags for Release" FORCE)
set(CMAKE_ASM_FLAGS_RELEASE "" CACHE STRING "ASM compiler flags for Release" FORCE)

# Linker script itself is added per-target in alif_platform_sdk.cmake, since
# it depends on the resolved ALIF_CMSIS_DFP_PATH + TARGET (RTSS-HP vs RTSS-HE
# each get their own memory map).
set(CMAKE_EXE_LINKER_FLAGS "${ALIF_MCU_FLAGS} --specs=nosys.specs -Wl,--gc-sections --entry=Reset_Handler -Wl,-Map=output.map" CACHE STRING "Linker flags for MCU" FORCE)

# Flashing goes through Alif's SETOOLS over UART-over-USB (see
# sdk/flash.cmake), not OpenOCD.
if(NOT PROBE)
    set(PROBE "setools")
endif()
