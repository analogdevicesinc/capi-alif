# capi-alif: Alif Ensemble platform backend for CAPI (DK-E8: RTSS-HP/RTSS-HE, APSS stub)
#
# Provides: capi_alif library target (included via add_subdirectory)
# Requires: CAPI interface headers via CAPI_DIR, CONFIG_* from Kconfig

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED CAPI_DIR)
    message(FATAL_ERROR "CAPI_DIR is not set. Point it at the CAPI interface repo.")
endif()

# Kconfig's Alif RTSS-core choice is the human-facing selector; TARGET (set by
# the board preset as <board>_<core>, e.g. dk-e8_rtss_hp) is CMake's. They
# must agree -- catches a board whose defconfig and CMakePresets.json TARGET
# were edited out of sync.
set(_alif_core_to_choice
    "rtss_hp=CONFIG_ALIF_RTSS_HP"
    "rtss_he=CONFIG_ALIF_RTSS_HE"
    "apss=CONFIG_ALIF_APSS"
)
set(_alif_boards dk-e8)

if(DEFINED TARGET)
    set(_alif_expected_choice "")
    foreach(_board ${_alif_boards})
        foreach(_pair ${_alif_core_to_choice})
            string(REPLACE "=" ";" _pair_list "${_pair}")
            list(GET _pair_list 0 _pair_core)
            list(GET _pair_list 1 _pair_choice)
            if("${TARGET}" STREQUAL "${_board}_${_pair_core}")
                set(_alif_expected_choice "${_pair_choice}")
            endif()
        endforeach()
    endforeach()
    if(_alif_expected_choice STREQUAL "")
        message(FATAL_ERROR "Unknown TARGET '${TARGET}' for capi-alif -- expected <board>_<core> "
                            "with board one of ${_alif_boards} and core rtss_hp, rtss_he or apss")
    endif()
    if(DEFINED ${_alif_expected_choice} AND NOT ${_alif_expected_choice})
        message(FATAL_ERROR
            "TARGET='${TARGET}' does not match the Kconfig Alif RTSS core "
            "choice (${_alif_expected_choice} is not set). Check this "
            "board's _defconfig against its CMakePresets.json TARGET.")
    endif()
endif()

function(capi_alif_sources_ifdef feature_toggle source)
    if(${feature_toggle})
        target_sources(capi_alif PRIVATE ${source} ${ARGN})
    endif()
endfunction()

# Core sources (always compiled): platform init/tick, IRQ, time, alloc --
# identical across RTSS-HP/RTSS-HE (same Cortex-M55 core, only clock speed
# and memory map differ, both supplied by capi_alif_config_sdk()).
add_library(capi_alif OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_platform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_irq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_alloc.c
    # Pad muxing for the bus drivers' `extra`, and for any application.
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_pinmux.c
)

capi_alif_sources_ifdef(CONFIG_UART_ALIF       ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_uart.c)
capi_alif_sources_ifdef(CONFIG_UART_STDIO_ALIF ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_uart_stdio.c)
capi_alif_sources_ifdef(CONFIG_GPIO_ALIF       ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_gpio.c)
capi_alif_sources_ifdef(CONFIG_MAILBOX_ALIF    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_mailbox.c)
capi_alif_sources_ifdef(CONFIG_SPI_ALIF        ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_spi.c)
capi_alif_sources_ifdef(CONFIG_I2C_ALIF        ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_i2c.c)
capi_alif_sources_ifdef(CONFIG_PINCTRL_ALIF    ${CMAKE_CURRENT_SOURCE_DIR}/platform/alif_capi_pinctrl.c)


target_include_directories(capi_alif PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CAPI_DIR}/inc
)

if(DEFINED ALIF_SDK_INCLUDE_DIRS)
    target_include_directories(capi_alif PUBLIC ${ALIF_SDK_INCLUDE_DIRS})
endif()

if(DEFINED ALIF_SDK_COMPILE_DEFS)
    target_compile_definitions(capi_alif PUBLIC ${ALIF_SDK_COMPILE_DEFS})
endif()

if(DEFINED ALIF_SDK_SOURCES)
    target_sources(capi_alif PRIVATE ${ALIF_SDK_SOURCES})

    # Alif's linker script keeps the boot/system code resident in MRAM with
    # patterns like `*startup*(.text .rodata*)` -- note `.text` matched
    # EXACTLY. Our global -ffunction-sections puts every function in its own
    # `.text.<name>` section, which those patterns do NOT match, so the boot
    # code silently falls through to the `*(.text*)` catch-all that targets
    # ITCM.
    #
    # That is fatal rather than merely suboptimal: the core is released with
    # VTOR at the MRAM vector table, reads the reset vector, and jumps into
    # ITCM -- which is still uninitialized, because the very code that
    # copies MRAM->ITCM is Reset_Handler itself. The result is a silent hang
    # before a single instruction of application code runs. `nm` on the .elf
    # shows it: Reset_Handler in the ITCM range rather than MRAM's
    # 0x8020xxxx.
    #
    # Compiling those sources without function/data sections restores the
    # plain `.text`/`.rodata` names the script expects. Only the files the
    # script actually pins to MRAM get this (ALIF_SDK_MRAM_SOURCES);
    # everything else keeps -ffunction-sections so --gc-sections can still
    # drop unused vendor code.
    if(DEFINED ALIF_SDK_FORCE_INCLUDE_SYSTEM_H)
        set_property(SOURCE ${ALIF_SDK_FORCE_INCLUDE_SYSTEM_H} APPEND PROPERTY
            COMPILE_OPTIONS "-include;stdint.h;-include;system.h")
    endif()

    if(DEFINED ALIF_SDK_FORCE_INCLUDE_RTE_COMPONENTS)
        set_property(SOURCE ${ALIF_SDK_FORCE_INCLUDE_RTE_COMPONENTS} APPEND PROPERTY
            COMPILE_OPTIONS "-include;RTE_Components.h")
    endif()

    if(DEFINED ALIF_SDK_MRAM_SOURCES)
        set_source_files_properties(${ALIF_SDK_MRAM_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-fno-function-sections;-fno-data-sections")
    endif()
endif()

add_library(capi::alif ALIAS capi_alif)
