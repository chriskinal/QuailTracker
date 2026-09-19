# Qt_U575VGT6 — CubeMX reference project

CubeMX configuration for the production STM32U575VGT6 (LQFP100, LDO) board.
Generate code here, then copy the init snippets you need into
`stm32/QuailTracker_U575/` by hand. **Never generate into the real project.**
CubeMX wipes `Middlewares/` and overwrites the linker script.

Only the `.ioc` is tracked. Everything CubeMX generates next to it is gitignored.

## Provenance

The original `/Users/chris/Code/Qt_U575VGT6` was lost in a disk crash and was
never committed. This `.ioc` was rebuilt on 2026-09-19 from firmware v0.10.20:
`main.c` (`SystemClock_Config`, `MX_*_Init`), `stm32u5xx_hal_msp.c`,
`stm32u5xx_it.c`, `app_freertos.c`, `FreeRTOSConfig.h`, and the pin table in
`hardware/stm32u575_pinout.md`.

It was hand-written, so CubeMX has not validated it yet. On first open, check
the items below and then save once, so CubeMX rewrites the file in its own
canonical form.

## Check in the CubeMX UI on first open

- **MDF1:** the pin-mode and filter key names were guessed. Confirm:
  - SITF0 is set to LF master SPI mode with clock source CCK0, on PD3 (SDI0) and PE9 (CCK0).
  - Filter0 uses bitstream 0 rising edge (L) and Filter1 uses bitstream 0 falling edge (R).
  - Both filters: Sinc4, decimation 64, HPF at 0.000625·Fpcm, SYNC_CONT mode, TRGO trigger.
  - Proc clock divider is 52 and the output clock (CCK0) is enabled.
- **RCC:** turn on MSI auto-calibration (MSIS PLL mode locked to LSE). The code
  calls `HAL_RCCEx_EnableMSIPLLMode()`, and the audio sample rate depends on it.
- **NVIC:** RTC_IRQn runs at priority 0, the same as the code. CubeMX may flag
  this because priority 0 is above the FreeRTOS syscall threshold (5). That's
  acceptable because the RTC ISR makes no RTOS calls.

## Not modelled here (these live in USER CODE only)

- The Stop 2 entry code switches PB12 (ESP32 CS) to EXTI12 falling at runtime.
- USART1 and USART3 RXNE interrupts are enabled by hand in `main()`, with
  custom handlers that feed RTOS queues.
- The SPI1 kernel clock is switched between HSI16 and PCLK2 at runtime by
  `SPI_SetSlow` and `SPI_SetFast` in `user_diskio.c`.
- The counting-semaphore override of `audioDmaSem`, plus the gpsTask,
  bridgeTask, inferTask and formatTask threads.
