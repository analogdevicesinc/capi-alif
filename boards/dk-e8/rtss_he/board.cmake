# Board defaults for the dk-e8/rtss_he sub-board. Shared DK-E8 hardware metadata
# (pinout, notes) lives in the parent boards/dk-e8/module.yml.
get_filename_component(_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(CAPI_ALIF_BOARD_DIR "${_BOARD_DIR}")

# Board facts capi_alif_config_sdk() needs (see sdk/alif_platform_sdk.cmake).
set(ALIF_CORE "rtss_he")
set(ALIF_SOC_DIR "AE822FA0E5597")
set(ALIF_BOARD_NAME "DK-E8")
# Secure Enclave UART rate SETOOLS talks at (see sdk/flash.cmake).
set(ALIF_SEUART_BAUD_DEFAULT "57600")
set(ALIF_DFP_BOARD "DevKit-e8")

# The board bsp (console + Click socket), shared by both cores.
get_filename_component(_BSP_DIR "${CMAKE_CURRENT_LIST_DIR}/../bsp" ABSOLUTE)
set(CAPI_ALIF_BOARD_BSP_DIR "${_BSP_DIR}")
# Kconfig has run by now: only the parts whose driver is enabled.
set(CAPI_ALIF_BOARD_BSP_SRC "${_BSP_DIR}/parameters.c")
if(CONFIG_GPIO_ALIF)
    list(APPEND CAPI_ALIF_BOARD_BSP_SRC "${_BSP_DIR}/parameters_gpio.c")
endif()
if(CONFIG_SPI_ALIF)
    list(APPEND CAPI_ALIF_BOARD_BSP_SRC "${_BSP_DIR}/parameters_spi.c")
endif()
if(CONFIG_I2C_ALIF)
    list(APPEND CAPI_ALIF_BOARD_BSP_SRC "${_BSP_DIR}/parameters_i2c.c")
endif()
if(CONFIG_PINCTRL_ALIF)
    list(APPEND CAPI_ALIF_BOARD_BSP_SRC "${_BSP_DIR}/parameters_pinctrl.c")
endif()
