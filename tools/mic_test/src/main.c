/*
 * Mic Test Firmware — LED brightness tracks PDM mic amplitude
 *
 * Standalone bare-metal firmware for testing IM72D128 PDM mic breakout
 * boards on the QuailTracker V2/V3 PCB. No FreeRTOS, no SD, no BLE.
 *
 * Hardware:
 *   MCU:  STM32U575VGT6 @ 160 MHz
 *   Mic:  IM72D128 PDM on ADF1 (PE9 CLK, PE10 DATA)
 *   LED:  PD13 (TIM4 CH2 PWM)
 *   Debug: SEGGER RTT via J-Link SWD
 *
 * Operation:
 *   - Silence = LED off
 *   - Sound = LED brightness proportional to peak amplitude
 *   - RTT terminal shows peak level for calibration
 */

#include "stm32u5xx_hal.h"
#include "SEGGER_RTT.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* printf via RTT */
static void rtt_printf(unsigned idx, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SEGGER_RTT_WriteString(idx, buf);
}

/* ---- Forward declarations ---- */
void SystemClock_Config(void);
void Error_Handler(void);

/* ---- ADF1 (PDM mic) ---- */
MDF_HandleTypeDef AdfHandle0;
MDF_FilterConfigTypeDef AdfFilterConfig0;

/* ---- GPDMA1 for ADF1 ---- */
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

/* ---- TIM4 for LED PWM ---- */
TIM_HandleTypeDef htim4;

/* ---- Audio DMA buffer ---- */
#define AUDIO_BUF_SIZE 1024
int32_t audioBuffer[AUDIO_BUF_SIZE];

/* ---- Peak tracking ---- */
volatile uint32_t peakAmplitude = 0;
uint32_t smoothedPeak = 0;

/* ================================================================== */
/*                         Init Functions                              */
/* ================================================================== */

static void MX_GPDMA1_Init(void)
{
    __HAL_RCC_GPDMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
}

static void MX_ADF1_Init(void)
{
    AdfHandle0.Instance = ADF1_Filter0;
    AdfHandle0.Init.CommonParam.ProcClockDivider = 52;
    AdfHandle0.Init.CommonParam.OutputClock.Activation = ENABLE;
    AdfHandle0.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
    AdfHandle0.Init.CommonParam.OutputClock.Divider = 1;
    AdfHandle0.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE;
    AdfHandle0.Init.SerialInterface.Activation = ENABLE;
    AdfHandle0.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
    AdfHandle0.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
    AdfHandle0.Init.SerialInterface.Threshold = 4;
    AdfHandle0.Init.FilterBistream = MDF_BITSTREAM0_FALLING;
    if (HAL_MDF_Init(&AdfHandle0) != HAL_OK)
        Error_Handler();

    AdfFilterConfig0.DataSource = MDF_DATA_SOURCE_BSMX;
    AdfFilterConfig0.Delay = 0;
    AdfFilterConfig0.CicMode = MDF_ONE_FILTER_SINC4;
    AdfFilterConfig0.DecimationRatio = 64;
    AdfFilterConfig0.Gain = 6;  /* +18 dB (6 * 3dB) */
    AdfFilterConfig0.ReshapeFilter.Activation = DISABLE;
    AdfFilterConfig0.HighPassFilter.Activation = ENABLE;
    AdfFilterConfig0.HighPassFilter.CutOffFrequency = MDF_HPF_CUTOFF_0_000625FPCM;
    AdfFilterConfig0.SoundActivity.Activation = DISABLE;
    AdfFilterConfig0.AcquisitionMode = MDF_MODE_ASYNC_CONT;
    AdfFilterConfig0.FifoThreshold = MDF_FIFO_THRESHOLD_NOT_EMPTY;
    AdfFilterConfig0.DiscardSamples = 0;
}

static void MX_TIM4_Init(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();

    /* TIM4 on APB1 = 160 MHz.  PSC=159 -> 1 MHz.  ARR=999 -> 1 kHz PWM. */
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 159;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 999;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
        Error_Handler();

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;  /* start with LED off */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
        Error_Handler();

    /* PD13 -> TIM4_CH2 (AF2) */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
}

/* ================================================================== */
/*                         HAL MSP Init                                */
/* ================================================================== */

void HAL_MspInit(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_MDF_MspInit(MDF_HandleTypeDef *hmdf)
{
    if (!IS_ADF_INSTANCE(hmdf->Instance)) return;

    /* ADF1 kernel clock = HCLK */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADF1;
    PeriphClkInit.Adf1ClockSelection = RCC_ADF1CLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
        Error_Handler();

    __HAL_RCC_ADF1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE9 = ADF1_CCK0, PE10 = ADF1_SDI0 (AF3) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF3_ADF1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* DMA linked-list node for ADF1 */
    DMA_NodeConfTypeDef NodeConfig = {0};
    NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
    NodeConfig.Init.Request = GPDMA1_REQUEST_ADF1_FLT0;
    NodeConfig.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    NodeConfig.Init.Direction = DMA_PERIPH_TO_MEMORY;
    NodeConfig.Init.SrcInc = DMA_SINC_FIXED;
    NodeConfig.Init.DestInc = DMA_DINC_INCREMENTED;
    NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_WORD;
    NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;
    NodeConfig.Init.SrcBurstLength = 1;
    NodeConfig.Init.DestBurstLength = 1;
    NodeConfig.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    NodeConfig.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    NodeConfig.Init.Mode = DMA_NORMAL;
    NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    NodeConfig.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;

    HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel0);
    HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel0, NULL, &Node_GPDMA1_Channel0);
    HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel0);

    handle_GPDMA1_Channel0.Instance = GPDMA1_Channel0;
    handle_GPDMA1_Channel0.InitLinkedList.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    handle_GPDMA1_Channel0.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
    handle_GPDMA1_Channel0.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    handle_GPDMA1_Channel0.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel0.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
    HAL_DMAEx_List_Init(&handle_GPDMA1_Channel0);
    HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel0, &List_GPDMA1_Channel0);

    __HAL_LINKDMA(hmdf, hdma, handle_GPDMA1_Channel0);
    HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel0, DMA_CHANNEL_NPRIV);
}

/* ================================================================== */
/*                       DMA Callbacks                                 */
/* ================================================================== */

static void ProcessHalf(int32_t *buf, uint32_t len)
{
    uint32_t peak = 0;
    for (uint32_t i = 0; i < len; i++) {
        int32_t sample = buf[i] >> 16;  /* Sinc4 left-justified -> int16 range */
        uint32_t mag = (uint32_t)abs(sample);
        if (mag > peak) peak = mag;
    }
    peakAmplitude = peak;
}

void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
    (void)hmdf;
    ProcessHalf(&audioBuffer[0], AUDIO_BUF_SIZE / 2);
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
    (void)hmdf;
    ProcessHalf(&audioBuffer[AUDIO_BUF_SIZE / 2], AUDIO_BUF_SIZE / 2);
}

/* ================================================================== */
/*                         IRQ Handlers                                */
/* ================================================================== */

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void GPDMA1_Channel0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
}

/* ================================================================== */
/*                            Main                                     */
/* ================================================================== */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Enable ICACHE for 160 MHz performance */
    HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY);
    HAL_ICACHE_Enable();

    /* Disable UCPD dead-battery pulldowns */
    HAL_PWREx_DisableUCPDDeadBattery();

    SEGGER_RTT_Init();
    rtt_printf(0, "\r\n=== Mic Test ===\r\n");
    rtt_printf(0, "LED brightness tracks PDM mic amplitude\r\n\r\n");

    MX_GPDMA1_Init();
    MX_ADF1_Init();
    MX_TIM4_Init();

    /* Start ADF1 DMA acquisition */
    MDF_DmaConfigTypeDef dmaConfig = {0};
    dmaConfig.Address = (uint32_t)audioBuffer;
    dmaConfig.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfig.MsbOnly = DISABLE;
    if (HAL_MDF_AcqStart_DMA(&AdfHandle0, &AdfFilterConfig0, &dmaConfig) != HAL_OK) {
        rtt_printf(0, "ADF1 DMA start FAILED\r\n");
        Error_Handler();
    }
    rtt_printf(0, "ADF1: Running (48kHz, Sinc4, gain=%d)\r\n", AdfFilterConfig0.Gain);

    uint32_t lastPrint = 0;

    while (1) {
        uint32_t peak = peakAmplitude;

        /* Exponential smoothing for stable LED */
        smoothedPeak = (smoothedPeak * 3 + peak) / 4;

        /* Map to PWM duty: 0-32767 -> 0-999 */
        uint32_t duty = smoothedPeak * 999 / 32768;
        if (duty > 999) duty = 999;
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, duty);

        /* Print peak level at 10 Hz */
        uint32_t now = HAL_GetTick();
        if (now - lastPrint >= 100) {
            lastPrint = now;

            /* Simple bar graph */
            char bar[33];
            uint32_t bars = smoothedPeak * 32 / 32768;
            if (bars > 32) bars = 32;
            for (uint32_t i = 0; i < 32; i++)
                bar[i] = (i < bars) ? '#' : '.';
            bar[32] = '\0';

            rtt_printf(0, "Peak: %5d  [%s]  PWM: %3d\r\n",
                              smoothedPeak, bar, duty);
        }

        HAL_Delay(10);  /* ~100 Hz update rate */
    }
}

/* ================================================================== */
/*                     System Clock Config                             */
/* ================================================================== */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    /* MSI 4 MHz -> PLL -> 160 MHz SYSCLK */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_4;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 80;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_PCLK3;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();
}

/* Minimal _sbrk for newlib heap (vsnprintf needs it) */
extern char _end;
void *_sbrk(int incr)
{
    static char *heap = 0;
    if (!heap) heap = &_end;
    char *prev = heap;
    heap += incr;
    return prev;
}

void Error_Handler(void)
{
    /* Solid LED on error */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOD, &g);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
    while (1) {}
}
