# capi-alif flashing -- via Alif's SETOOLS over UART-over-USB, not OpenOCD.
#
# SETOOLS' own bundled binaries crash on the SEUART's non-standard baud
# rate (see
# sdk/tools/alif_seuart_bridge.py's header for the full story) -- the
# generated `flash` target below launches that bridge itself, then delegates
# to sdk/tools/alif_flash_multicore.py for the actual ATOC generation/write.
#
# Always goes through the multicore wrapper, even when flashing a single
# core: Alif's boot chain expects an entry for every application core in
# the ATOC. A TOC holding only the core being flashed writes successfully
# and then nothing boots, with no error, so the wrapper fills in the vendor
# stub for whichever core is not being flashed.

set(_alif_flash_dir "${CMAKE_CURRENT_LIST_DIR}")

function(capi_alif_add_flash_target TARGET_NAME)
    # The flash tool writes <target>.bin. capi calls only
    # capi_<platform>_add_flash_target(), never _post_build(), so the .bin
    # has to be set up from here too; post_build runs once either way.
    capi_alif_post_build(${TARGET_NAME})

    if(NOT ALIF_SETOOLS_PATH)
        if(DEFINED ENV{ALIF_SETOOLS_PATH})
            set(ALIF_SETOOLS_PATH "$ENV{ALIF_SETOOLS_PATH}")
        endif()
    endif()
    if(NOT ALIF_SETOOLS_PATH)
        message(WARNING "ALIF_SETOOLS_PATH not set -- flash target unavailable. "
                         "Point it at your installed app-release-exec-linux directory.")
        return()
    endif()

    if(NOT ALIF_SEUART_DEVICE)
        set(ALIF_SEUART_DEVICE "/dev/ttyACM0" CACHE STRING "SEUART USB-CDC device node")
    endif()
    # The Secure Enclave's UART runs at a fixed, family-specific rate
    # (SETOOLS utils/featuresDB.db rev_baud_rate): 55000 on the E7, 57600
    # on the E8. The board's board.cmake sets ALIF_SEUART_BAUD_DEFAULT.
    if(NOT ALIF_SEUART_BAUD_DEFAULT)
        set(ALIF_SEUART_BAUD_DEFAULT "55000")
    endif()
    if(NOT ALIF_SEUART_BAUD)
        set(ALIF_SEUART_BAUD "${ALIF_SEUART_BAUD_DEFAULT}" CACHE STRING "SEUART real baud rate")
    endif()

    find_program(_alif_python3 python3)

    if("${TARGET}" MATCHES "_rtss_hp$")
        set(_alif_core_flag "--rtss-hp")
        set(_alif_peer_flag "--peer-rtss-he")
        set(_alif_peer_dir "rtss_he")
    elseif("${TARGET}" MATCHES "_rtss_he$")
        set(_alif_core_flag "--rtss-he")
        set(_alif_peer_flag "--peer-rtss-hp")
        set(_alif_peer_dir "rtss_hp")
    else()
        message(WARNING "capi_alif_add_flash_target: unsupported TARGET '${TARGET}' -- flash target unavailable.")
        return()
    endif()

    # Each core of one application is configured into its own build dir, named
    # after the core and sibling to this one (build/rtss_hp, build/rtss_he).
    # A write replaces the whole ATOC, so the peer has to ride along or it gets
    # stubbed and stops running; the script skips it when it isn't built yet.
    set(_alif_peer_image
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/../${_alif_peer_dir}/$<TARGET_FILE_BASE_NAME:${TARGET_NAME}>.bin")

    add_custom_target(flash
        COMMAND ${_alif_python3} "${_alif_flash_dir}/tools/alif_flash_multicore.py"
                --setools-dir "${ALIF_SETOOLS_PATH}"
                --bridge-script "${_alif_flash_dir}/tools/alif_seuart_bridge.py"
                --device "${ALIF_SEUART_DEVICE}"
                --baud "${ALIF_SEUART_BAUD}"
                ${_alif_core_flag} "$<TARGET_FILE_DIR:${TARGET_NAME}>/$<TARGET_FILE_BASE_NAME:${TARGET_NAME}>.bin"
                ${_alif_peer_flag} "${_alif_peer_image}"
        DEPENDS ${TARGET_NAME}
        COMMENT "Flashing ${TARGET_NAME} via SETOOLS"
        VERBATIM
        USES_TERMINAL
    )
endfunction()

function(capi_alif_post_build TARGET_NAME)
    # Called by an SDK that knows _post_build, and by add_flash_target for
    # one that does not: add the commands once.
    get_target_property(_alif_post_build_done ${TARGET_NAME} CAPI_ALIF_POST_BUILD)
    if(_alif_post_build_done)
        return()
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES CAPI_ALIF_POST_BUILD TRUE)

    find_program(_objcopy arm-none-eabi-objcopy)
    find_program(_size arm-none-eabi-size)

    if(_objcopy)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${_objcopy} -O binary $<TARGET_FILE:${TARGET_NAME}>
                    $<TARGET_FILE_DIR:${TARGET_NAME}>/$<TARGET_FILE_BASE_NAME:${TARGET_NAME}>.bin
            COMMENT "Generating .bin"
        )
    endif()

    if(_size)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${_size} --format=berkeley $<TARGET_FILE:${TARGET_NAME}>
            COMMENT "Binary size:"
        )
    endif()
endfunction()
