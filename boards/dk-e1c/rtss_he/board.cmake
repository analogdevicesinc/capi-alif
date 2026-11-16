# Board defaults for the dk-e1c/rtss_he sub-board, the E1C's only core.
get_filename_component(_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(CAPI_ALIF_BOARD_DIR "${_BOARD_DIR}")

# Board facts capi_alif_config_sdk() needs (see sdk/alif_platform_sdk.cmake).
set(ALIF_CORE "rtss_he")
set(ALIF_SOC_DIR "AE1C1F4051920")
set(ALIF_BOARD_NAME "DK-E1C")
set(ALIF_DFP_BOARD "DevKit-e1c")
# Secure Enclave UART rate SETOOLS talks at (see sdk/flash.cmake).
set(ALIF_SEUART_BAUD_DEFAULT "57600")
# One application core and no APSS: the boot table holds only this image.
set(ALIF_SINGLE_CORE TRUE)

# The board bsp (console + Click socket).
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
