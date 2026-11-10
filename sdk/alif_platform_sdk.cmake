# capi-alif SDK configuration -- called by capi_create_application()
# (via cmake_language(CALL "capi_alif_config_sdk" ...)) before
# add_subdirectory(capi-alif).
#
# Alif's alif_ensemble-cmsis-dfp ships per-example *generated* device config
# (RTE_Device.h, core_config.h, app_mem_regions.h, linker_gnu_*.ld.src) meant
# to be copied into and owned by each project. capi-alif vendors one such
# baseline per board core -- boards/<board>/rtss_{hp,he}/device_config/ --
# instead of trying to regenerate them from CMSIS-Toolbox at configure time.
# These vendored files carry Alif's own license header (see
# ALIF_CMSIS_DFP_PATH/License.txt); retain it if you regenerate/replace them.
#
# Everything board-specific comes from the core's board.cmake, included by
# capi_create_application() before this runs:
#   CAPI_ALIF_BOARD_DIR  the core's directory (its device_config/ is used)
#   ALIF_CORE            rtss_hp | rtss_he
#   ALIF_SOC_DIR         the DFP's Device/soc/<part> directory name
#   ALIF_DFP_BOARD       the DFP's Boards/<board> directory name (pins.h,
#                        board_defs.h for board_config.c)
#   ALIF_BOARD_NAME      optional, the board's name for applications, as
#                        the ALIF_BOARD_NAME string macro (e.g. "DK-E8")

set(_alif_sdk_dir "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(_alif_platform_dir "${_alif_sdk_dir}/.." ABSOLUTE)

function(capi_alif_config_sdk APP_NAME)
    foreach(_var CAPI_ALIF_BOARD_DIR ALIF_CORE ALIF_SOC_DIR ALIF_DFP_BOARD)
        if(NOT ${_var})
            message(FATAL_ERROR "capi_alif_config_sdk: ${_var} is not set -- "
                                "the board's board.cmake must set it (TARGET '${TARGET}')")
        endif()
    endforeach()
    if("${ALIF_CORE}" STREQUAL "rtss_hp")
        set(_core_upper "HP")
    elseif("${ALIF_CORE}" STREQUAL "rtss_he")
        set(_core_upper "HE")
    else()
        message(FATAL_ERROR "capi_alif_config_sdk: unsupported core '${ALIF_CORE}' "
                            "(TARGET '${TARGET}'): only rtss_hp and rtss_he build")
    endif()
    set(_core_dir "${ALIF_CORE}")

    set(_dfp "${ALIF_CMSIS_DFP_PATH}")
    set(_dev_cfg "${CAPI_ALIF_BOARD_DIR}/device_config")
    string(TOLOWER "${_core_upper}" _core_lower)
    set(_soc_include "${_dfp}/Device/soc/${ALIF_SOC_DIR}/include/rtss_${_core_lower}")

    # --- Preprocess the vendored GNU linker template into a real .ld -----
    # linker_gnu_mram.ld.src is a GNU ld script that itself needs C
    # preprocessing (#include "app_mem_regions.h", #if __HAS_BULK_SRAM),
    # which is Alif's convention for these templates. Boots and links from
    # MRAM (XIP), matching how SETOOLS' app-write-mram programs the device.
    # The *_tcm.ld.src alternative (SRAM-resident, for iterating without
    # reflashing MRAM) is vendored too but not wired up.
    set(_ld_out "${CMAKE_BINARY_DIR}/alif_generated/${_core_dir}_mram.ld")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/alif_generated")
    add_custom_command(
        OUTPUT "${_ld_out}"
        COMMAND ${CMAKE_C_COMPILER} -E -P -xc
                -I"${_dev_cfg}"
                "${_dev_cfg}/linker_gnu_mram.ld.src"
                -o "${_ld_out}"
        DEPENDS "${_dev_cfg}/linker_gnu_mram.ld.src" "${_dev_cfg}/app_mem_regions.h"
        COMMENT "Preprocessing Alif linker template for ${TARGET}"
        VERBATIM
    )
    add_custom_target(${APP_NAME}_alif_linker_script DEPENDS "${_ld_out}")

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${_ld_out}" PARENT_SCOPE)

    # --- Include dirs / compile defs for capi_alif + the application -----
    # CMSIS_6_PATH's Driver/Include supplies the *base* ARM CMSIS-Driver
    # headers (Driver_USART.h, Driver_GPIO.h, Driver_Common.h) that
    # Alif_CMSIS's own Driver_USART.c/Driver_GPIO.c implement against --
    # not part of the DFP itself (see toolchain.cmake's resolution).
    set(ALIF_SDK_INCLUDE_DIRS
        "${_dev_cfg}"
        "${_soc_include}"
        "${_dfp}/Device/soc/${ALIF_SOC_DIR}/include"
        "${_dfp}/Device/core/common/include"
        "${_dfp}/Device/system/include"
        "${_dfp}/Alif_CMSIS/Include"
        "${_dfp}/drivers/include"
        # The board's Conductor-tool-generated pin/GPIO tables (pins.h,
        # gpios.h, board_defs.h) that board_config.c walks -- this is what
        # actually routes UART4/GPIO to physical pins. Board-specific, but
        # consumed by the generic board_config.c below.
        "${_dfp}/Boards/${ALIF_DFP_BOARD}"
        "${_dfp}/libs/board_config"
        # board_config.h pulls in se_services_port.h (board_clocks_config()
        # talks to the Secure Enclave to request clocks); headers only --
        # capi-alif does not use the SE services API itself.
        "${_dfp}/se_services/include"
        "${_dfp}/se_services/port/include"
        "${CMSIS_6_PATH}/CMSIS/Driver/Include"
        "${CMSIS_6_PATH}/CMSIS/Core/Include"
        PARENT_SCOPE
    )
    if(NOT ALIF_BOARD_NAME)
        set(ALIF_BOARD_NAME "${ALIF_DFP_BOARD}")
    endif()
    set(ALIF_SDK_COMPILE_DEFS
        "RTSS_${_core_upper}=1"
        "ALIF_BOARD_NAME=\"${ALIF_BOARD_NAME}\""
        "CMSIS_device_header=<soc.h>"
        PARENT_SCOPE
    )

    # --- Startup + system init (shared source, per-core config via soc.h) -
    # Driver_USART.c/Driver_GPIO.c and the MHU driver are included
    # unconditionally rather than gated by CONFIG_UART_ALIF/CONFIG_GPIO_ALIF/
    # CONFIG_MAILBOX_ALIF: each driver's own RTE_Device.h RTE_* flags already
    # gate what compiles inside them, so the cost is a few extra KB in the
    # .elf when a disabled interface's driver links in.
    set(ALIF_SDK_SOURCES
        "${_dfp}/Device/core/common/source/startup.c"
        "${_dfp}/Device/core/common/source/system.c"
        "${_dfp}/Device/core/common/source/vectors.c"
        "${_dfp}/Device/core/common/source/mpu.c"
        "${_dfp}/Device/core/common/source/sau_tcm_ns_setup.c"
        "${_dfp}/Device/core/common/source/tgu.c"
        "${_dfp}/Device/system/source/sys_clocks.c"
        "${_dfp}/Device/system/source/sys_utils.c"
        "${_dfp}/Alif_CMSIS/Source/Driver_USART.c"
        "${_dfp}/Alif_CMSIS/Source/Driver_GPIO.c"
        "${_dfp}/drivers/source/uart.c"
        # Pin muxing. Without these the UART/GPIO peripherals initialize
        # fine but their signals never reach the package pins -- symptom is
        # a silent console with no error anywhere. capi_platform_init()
        # calls board_pins_config() (see alif_capi_platform.c).
        "${_dfp}/drivers/source/pinconf.c"
        "${_dfp}/libs/board_config/board_config.c"
        "${_dfp}/drivers/source/mhu_driver.c"
        "${_dfp}/drivers/source/mhu_sender.c"
        "${_dfp}/drivers/source/mhu_receiver.c"
    )
    # Bus controllers, only when their capi driver is configured in.
    if(CONFIG_SPI_ALIF)
        list(APPEND ALIF_SDK_SOURCES
            "${_dfp}/Alif_CMSIS/Source/Driver_SPI.c"
            "${_dfp}/drivers/source/spi.c")
    endif()
    if(CONFIG_I2C_ALIF)
        list(APPEND ALIF_SDK_SOURCES
            "${_dfp}/Alif_CMSIS/Source/Driver_I2C.c"
            "${_dfp}/drivers/source/i2c.c"
            # The I3C controller as an I2C one (identifier 4), where the
            # DevKit's on-board IMUs are wired.
            "${_dfp}/Alif_CMSIS/Source/Driver_I3C.c"
            "${_dfp}/drivers/source/i3c.c")
    endif()
    set(ALIF_SDK_SOURCES "${ALIF_SDK_SOURCES}" PARENT_SCOPE)
    # Driver_SPI_Private.h reads SystemCoreClock without including the
    # header that declares it (Device/core/common/include/system.h).
    if(CONFIG_SPI_ALIF)
        set(ALIF_SDK_FORCE_INCLUDE_SYSTEM_H
            "${_dfp}/Alif_CMSIS/Source/Driver_SPI.c" PARENT_SCOPE)
    endif()
    # Driver_I3C.c tests its RTE_Drivers_* flag without including the
    # RTE_Components.h that defines it, so they would compile to nothing.
    if(CONFIG_I2C_ALIF)
        set(ALIF_SDK_FORCE_INCLUDE_RTE_COMPONENTS
            "${_dfp}/Alif_CMSIS/Source/Driver_I3C.c" PARENT_SCOPE)
    endif()

    # Sources the linker script pins to MRAM by filename pattern -- see
    # linker_gnu_mram.ld.src's `.startup.at_mram` block, which lists
    # *startup*, *system*, *sys_*, *pm*, *tgu*, *mpu* and
    # *sau_tcm_ns_setup* with an EXACT `.text` match. These must be
    # compiled without -ffunction-sections or they land in ITCM instead and
    # the device hangs at reset (see capi-alif's root CMakeLists.txt for the
    # full explanation). Everything else keeps -ffunction-sections so
    # --gc-sections can still drop unused vendor code -- notably
    # board_config.c's board_clocks_config(), which would otherwise pull in
    # the SE-services library we don't link.
    set(ALIF_SDK_MRAM_SOURCES
        "${_dfp}/Device/core/common/source/startup.c"
        "${_dfp}/Device/core/common/source/system.c"
        "${_dfp}/Device/core/common/source/mpu.c"
        "${_dfp}/Device/core/common/source/sau_tcm_ns_setup.c"
        "${_dfp}/Device/core/common/source/tgu.c"
        "${_dfp}/Device/system/source/sys_clocks.c"
        "${_dfp}/Device/system/source/sys_utils.c"
        PARENT_SCOPE
    )

    add_dependencies(${APP_NAME} ${APP_NAME}_alif_linker_script)
endfunction()
