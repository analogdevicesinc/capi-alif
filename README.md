# capi-alif

The Alif platform for [CAPI](https://github.com/analogdevicesinc/capi).
It implements the CAPI interfaces on the Alif CMSIS Device Family Pack.

## CAPI setup

You need CMake 3.27+, Ninja, Python 3 and network access. Clone capi, this
repo and an application into one workspace:

```bash
mkdir ~/workspace && cd ~/workspace
git clone https://github.com/analogdevicesinc/capi
git clone https://github.com/analogdevicesinc/capi-alif
git clone https://github.com/analogdevicesinc/capi-hello-world
```

```bash
export CAPI_WORKSPACE=~/workspace
export CAPI_DIR=~/workspace/capi
```

The application uses this checkout. To use a checkout in a different
directory, set `CAPI_ALIF_DIR`.

## Platform setup

Install `arm-none-eabi-gcc` with newlib. Clone Alif's CMSIS DFP and ARM's
CMSIS_6 — the DFP ships Alif's driver implementations but not the base
CMSIS-Driver headers they implement against:

```bash
git clone https://github.com/alifsemi/alif_ensemble-cmsis-dfp
git clone https://github.com/ARM-software/CMSIS_6
export ALIF_CMSIS_DFP_PATH=~/workspace/alif_ensemble-cmsis-dfp
export CMSIS_6_PATH=~/workspace/CMSIS_6
```

Not `sdk-alif` or `hal_alif`: those are Alif's Zephyr SDK and HAL module and
carry no bare-metal startup, linker scripts or register-level drivers.

To flash, set `ALIF_SETOOLS_PATH` to an Alif Security Toolkit directory and
point the tools at the part on your board, as marked on the chip (run it
again when you switch boards):

```bash
export ALIF_SETOOLS_PATH=/path/to/app-release-exec-linux
cd "$ALIF_SETOOLS_PATH"
./tools-config -p "E8 (AE822FA0E5597LS0) - 5.5 MRAM / 9.75 SRAM" -r A1     # DK-E8
./tools-config -p "E1C (AE1C1F4051920HH0) - 1.86 MRAM / 2.0 SRAM" -r A8    # DK-E1C
./tools-config -p "B1 (AB1C1F4M51820PH0) - 1.8 MRAM / 2.0 SRAM" -r A8      # DK-B1
```

## Build and flash

Run in `~/workspace/capi-hello-world`. Each core is a separate build:

```bash
cmake --preset <preset>
cmake --build <build dir> --target flash
```

| Board | Core | Preset | Build dir |
|---|---|---|---|
| DK-E8 | RTSS-HP, Cortex-M55 @400MHz | `dk-e8-rtss-hp` | `build-alif/rtss_hp` |
| DK-E8 | RTSS-HE, Cortex-M55 @160MHz | `dk-e8-rtss-he` | `build-alif/rtss_he` |
| DK-E1C | RTSS-HE, Cortex-M55 @160MHz | `dk-e1c-rtss-he` | `build-alif/dk-e1c` |
| DK-B1 | RTSS-HE, Cortex-M55 @160MHz | `dk-b1-rtss-he` | `build-alif/dk-b1` |

Interfaces: `uart` (and stdio), `gpio`, `spi`, `i2c`, `pinctrl`, `mailbox`,
`irq`, `time`, `alloc`.

## Notes

- A write replaces the whole boot table, so on DK-E8 a core left out of one
  is stubbed and stops running. Build both cores, then flash once from
  either — the `flash` target picks up the sibling image and writes both.
- Flashing needs the RESET button pressed when the tool asks for it, and
  uses the PRG USB port's Secure-Enclave UART: set switch **SW4** to **SE**.
  Move it to **U2** to see the console.
- RTSS-HP has run on a DK-E8. RTSS-HE builds but has not been run on one.
  DK-B1 has run on a board, its Click SPI, I2C and GPIOs included. DK-E1C,
  wired the same as DK-B1, builds but has not been run.
- The DFP has no Balletto part. DK-B1 builds against the E1C's SoC files,
  whose peripherals, interrupts and pin functions the B1 shares, with its
  own pin tables (`boards/dk-b1/dfp_board/`) and memory map: the first 40 KB of
  DTCM are left to the BLE ROM. The radio is not supported.
- APSS (Cortex-A32) is declared but not buildable; selecting it fails at
  configure rather than producing a broken image.
- `boards/*/*/device_config/` vendors Alif's generated device
  configuration under Alif's own licences, with the UART blocking-mode flags
  turned on. Regenerating it without re-applying that change makes the
  console hang with no diagnostic.
- SPI and I2C take their pads in the controller's `extra`, as a list of
  `struct alif_capi_pin` (`platform/alif_capi_pinmux.h`); the driver muxes
  them at init.
- Timers, GPIO interrupts, async transfers and the NPU are not implemented.

## License

BSD-3-Clause, except the vendored files noted above.
