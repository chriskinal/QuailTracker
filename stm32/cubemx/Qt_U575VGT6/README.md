# Qt_U575VGT6 — CubeMX reference project

CubeMX configuration for the production STM32U575VGT6 (LQFP100, LDO) board.
Generate code here, then copy the init snippets you need into
`stm32/QuailTracker_U575/` by hand. **Never generate into the real project.**
CubeMX wipes `Middlewares/` and overwrites the linker script.

Only the `.ioc` is tracked. Everything CubeMX generates next to it is gitignored.

## Provenance

The original `/Users/chris/Code/Qt_U575VGT6` was lost in a disk crash and was
never committed. On 2026-09-19 this `.ioc` was rebuilt from firmware v0.10.20:
`main.c` (`SystemClock_Config`, `MX_*_Init`), `stm32u5xx_hal_msp.c`,
`stm32u5xx_it.c`, `app_freertos.c`, `FreeRTOSConfig.h`, and the pin table in
`hardware/stm32u575_pinout.md`. It was then checked field by field in the
CubeMX UI and saved by CubeMX, so it opens with no warnings.

If CubeMX offers to migrate to a newer FW_U5 package, choose **Continue**.
The repo's HAL drivers are from FW_U5 V1.8.0 (HAL 1.6.2), so generated init
code has to target that same HAL. To move to a newer package, update
`Drivers/` in the firmware first, then migrate this `.ioc` to match.

## Where things live in the UI

- **MDF1:** listed under Computing. Mode panel:
  - Instance 0: DFLT0 plus SITF0, with Common Clock 0 as the clock source.
  - Instance 1: DFLT1 only, with SITF1 disabled. It reads SITF0's bitstream
    on the falling front.
  - Common Clocks: CCK0 only. It has to be enabled here before
    "Output Clock Activation" can be turned on.
- **MSI auto-calibration:** RCC → Parameter Settings → "MSIS/MSIK Auto
  Calibration" = MSIS.
- **GPDMA priority labels:** "High" = `DMA_LOW_PRIORITY_HIGH_WEIGHT`, which is
  what the firmware uses. "Low" is the low-weight variant.
- **RTC IRQ at priority 0:** requires unchecking "Uses FreeRTOS functions".

## Known differences from the firmware (deliberate)

- **MDF1 output-clock trigger edge** (Common Parameters → Trigger Edge) is
  Falling. The firmware uses Rising, but CubeMX greys Rising out. TRGO is a
  single pulse, so either edge works.
- **MDF1 filter gain** is 6, the runtime value that `main()` sets before
  starting acquisition. `MX_MDF1_Init` itself initializes the gain to 0.

## Not modelled here (these live in USER CODE only)

- The Stop 2 entry code switches PB12 (ESP32 CS) to EXTI12 falling at runtime.
- USART1 and USART3 RXNE interrupts are enabled by hand in `main()`, with
  custom handlers that feed RTOS queues.
- The SPI1 kernel clock is switched between HSI16 and PCLK2 at runtime by
  `SPI_SetSlow` and `SPI_SetFast` in `user_diskio.c`.
- The counting-semaphore override of `audioDmaSem`, plus the gpsTask,
  bridgeTask, inferTask and formatTask threads.
