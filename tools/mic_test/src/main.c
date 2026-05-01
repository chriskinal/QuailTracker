/*
 * Mic Test Firmware — LED brightness tracks PDM mic amplitude (stereo)
 *
 * Standalone bare-metal firmware for testing IM72D128 PDM mic breakout
 * boards on the QuailTracker V5 PCB. No FreeRTOS, no SD, no companion radio.
 *
 * Hardware:
 *   MCU:   STM32U575VGT6 @ 160 MHz
 *   Mics:  IM72D128 PDM stereo on MDF1
 *          PE9 = MDF1_CCK0 (clock, AF6)
 *          PD3 = MDF1_SDI0 (data,  AF6) — interleaved L/R bitstream
 *          Filter0 = Left  (PCB silkscreen "Left",  L/R sel = GND, rising edge)
 *          Filter1 = Right (PCB silkscreen "Right", L/R sel = VDD, falling edge)
 *          (Edge-to-channel mapping matches PCB silkscreen, not the IM72D128
 *           datasheet's nominal L/R convention.)
 *   LED:   PD13 (TIM4 CH2 PWM)
 *   Debug: SEGGER RTT via J-Link SWD
 *
 * Operation:
 *   - Silence = LED off
 *   - Sound   = LED brightness proportional to MAX(peakL, peakR)
 *   - RTT terminal shows separate L and R bar graphs for A/B comparison
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
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SEGGER_RTT_WriteString(idx, buf);
}

/* ---- Forward declarations ---- */
void SystemClock_Config(void);
void Error_Handler(void);

/* ---- MDF1 (stereo PDM mic) ---- */
MDF_HandleTypeDef MdfHandle0;           /* Left  channel (Filter0, rising edge,  GND-side) */
MDF_HandleTypeDef MdfHandle1;           /* Right channel (Filter1, falling edge, VDD-side) */
MDF_FilterConfigTypeDef MdfFilterConfig0;
MDF_FilterConfigTypeDef MdfFilterConfig1;

/* ---- GPDMA1: Channel0 → MDF1_FLT0 (L), Channel1 → MDF1_FLT1 (R) ---- */
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

DMA_NodeTypeDef Node_GPDMA1_Channel1;
DMA_QListTypeDef List_GPDMA1_Channel1;
DMA_HandleTypeDef handle_GPDMA1_Channel1;

/* ---- TIM4 for LED PWM ---- */
TIM_HandleTypeDef htim4;

/* ---- Audio DMA buffers (per channel) ---- */
#define AUDIO_BUF_SIZE 1024
int32_t audioBufferL[AUDIO_BUF_SIZE];
int32_t audioBufferR[AUDIO_BUF_SIZE];

/* ---- Peak tracking ---- */
volatile uint32_t peakL = 0;
volatile uint32_t peakR = 0;
uint32_t smoothedL = 0;
uint32_t smoothedR = 0;

/* ================================================================== */
/*                         Init Functions                              */
/* ================================================================== */

static void MX_GPDMA1_Init(void)
{
    __HAL_RCC_GPDMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
}

static void MX_MDF1_Init(void)
{
    /* Filter0 — Left channel (rising edge, GND-side mic).
     * Owns the serial interface and output clock; runs continuously and
     * generates TRGO so Filter1 can start sample-aligned. */
    MdfHandle0.Instance = MDF1_Filter0;
    MdfHandle0.Init.CommonParam.ProcClockDivider = 52;  /* 160MHz/52 = 3.077MHz PDM clock */
    MdfHandle0.Init.CommonParam.OutputClock.Activation = ENABLE;
    MdfHandle0.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
    MdfHandle0.Init.CommonParam.OutputClock.Divider = 1;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Activation = ENABLE;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Source = MDF_CLOCK_TRIG_TRGO;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Edge = MDF_CLOCK_TRIG_RISING_EDGE;
    MdfHandle0.Init.SerialInterface.Activation = ENABLE;
    MdfHandle0.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
    MdfHandle0.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
    MdfHandle0.Init.SerialInterface.Threshold = 4;
    MdfHandle0.Init.FilterBistream = MDF_BITSTREAM0_RISING;
    if (HAL_MDF_Init(&MdfHandle0) != HAL_OK)
        Error_Handler();

    /* Filter1 — Right channel (falling edge, VDD-side mic).
     * Reads from SITF0 via the bitstream mixer; serial interface itself disabled. */
    MdfHandle1.Instance = MDF1_Filter1;
    MdfHandle1.Init.CommonParam.ProcClockDivider = 52;
    MdfHandle1.Init.CommonParam.OutputClock.Activation = DISABLE;
    MdfHandle1.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
    MdfHandle1.Init.CommonParam.OutputClock.Divider = 1;
    MdfHandle1.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE;
    MdfHandle1.Init.SerialInterface.Activation = DISABLE;
    MdfHandle1.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
    MdfHandle1.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
    MdfHandle1.Init.SerialInterface.Threshold = 4;
    MdfHandle1.Init.FilterBistream = MDF_BITSTREAM0_FALLING;
    if (HAL_MDF_Init(&MdfHandle1) != HAL_OK)
        Error_Handler();

    /* Shared filter config — SYNC_CONT, both wait on TRGO for sample-aligned start */
    MdfFilterConfig0.DataSource = MDF_DATA_SOURCE_BSMX;
    MdfFilterConfig0.Delay = 0;
    MdfFilterConfig0.CicMode = MDF_ONE_FILTER_SINC4;
    MdfFilterConfig0.DecimationRatio = 64;
    MdfFilterConfig0.Gain = 6;  /* +18 dB (6 * 3dB) */
    MdfFilterConfig0.ReshapeFilter.Activation = DISABLE;
    MdfFilterConfig0.HighPassFilter.Activation = ENABLE;
    MdfFilterConfig0.HighPassFilter.CutOffFrequency = MDF_HPF_CUTOFF_0_000625FPCM;
    MdfFilterConfig0.SoundActivity.Activation = DISABLE;
    MdfFilterConfig0.AcquisitionMode = MDF_MODE_SYNC_CONT;
    MdfFilterConfig0.Trigger.Source = MDF_FILTER_TRIG_TRGO;
    MdfFilterConfig0.Trigger.Edge = MDF_FILTER_TRIG_RISING_EDGE;
    MdfFilterConfig0.FifoThreshold = MDF_FIFO_THRESHOLD_NOT_EMPTY;
    MdfFilterConfig0.DiscardSamples = 0;

    memcpy(&MdfFilterConfig1, &MdfFilterConfig0, sizeof(MdfFilterConfig1));
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
    if (!IS_MDF_INSTANCE(hmdf->Instance)) return;

    /* MDF1 kernel clock = HCLK */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_MDF1;
    PeriphClkInit.Mdf1ClockSelection = RCC_MDF1CLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
        Error_Handler();

    __HAL_RCC_MDF1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PE9 = MDF1_CCK0 (AF6) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF6_MDF1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* PD3 = MDF1_SDI0 (AF6) */
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF6_MDF1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* DMA Channel 0 → MDF1_FLT0 (Left) — linked-list circular */
    if (hmdf->Instance == MDF1_Filter0) {
        DMA_NodeConfTypeDef NodeConfig = {0};
        NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
        NodeConfig.Init.Request = GPDMA1_REQUEST_MDF1_FLT0;
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

    /* DMA Channel 1 → MDF1_FLT1 (Right) — linked-list circular */
    if (hmdf->Instance == MDF1_Filter1) {
        DMA_NodeConfTypeDef NodeConfig = {0};
        NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
        NodeConfig.Init.Request = GPDMA1_REQUEST_MDF1_FLT1;
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

        HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel1);
        HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel1, NULL, &Node_GPDMA1_Channel1);
        HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel1);

        handle_GPDMA1_Channel1.Instance = GPDMA1_Channel1;
        handle_GPDMA1_Channel1.InitLinkedList.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
        handle_GPDMA1_Channel1.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
        handle_GPDMA1_Channel1.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
        handle_GPDMA1_Channel1.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel1.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
        HAL_DMAEx_List_Init(&handle_GPDMA1_Channel1);
        HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel1, &List_GPDMA1_Channel1);

        __HAL_LINKDMA(hmdf, hdma, handle_GPDMA1_Channel1);
        HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel1, DMA_CHANNEL_NPRIV);
    }
}

/* ================================================================== */
/*                       DMA Callbacks                                 */
/* ================================================================== */

static uint32_t scan_peak(const int32_t *buf, uint32_t len)
{
    uint32_t peak = 0;
    for (uint32_t i = 0; i < len; i++) {
        int32_t sample = buf[i] >> 16;  /* Sinc4 left-justified -> int16 range */
        uint32_t mag = (uint32_t)abs(sample);
        if (mag > peak) peak = mag;
    }
    return peak;
}

void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
    if (hmdf->Instance == MDF1_Filter0)
        peakL = scan_peak(&audioBufferL[0], AUDIO_BUF_SIZE / 2);
    else if (hmdf->Instance == MDF1_Filter1)
        peakR = scan_peak(&audioBufferR[0], AUDIO_BUF_SIZE / 2);
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
    if (hmdf->Instance == MDF1_Filter0)
        peakL = scan_peak(&audioBufferL[AUDIO_BUF_SIZE / 2], AUDIO_BUF_SIZE / 2);
    else if (hmdf->Instance == MDF1_Filter1)
        peakR = scan_peak(&audioBufferR[AUDIO_BUF_SIZE / 2], AUDIO_BUF_SIZE / 2);
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

void GPDMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
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
    rtt_printf(0, "\r\n=== Mic Test (Stereo) ===\r\n");
    rtt_printf(0, "LED brightness tracks MAX(L, R); RTT shows L/R bars separately\r\n\r\n");

    MX_GPDMA1_Init();
    MX_MDF1_Init();
    MX_TIM4_Init();

    /* Start both filters in SYNC_CONT (waiting on TRGO) */
    MDF_DmaConfigTypeDef dmaConfigL = {0};
    dmaConfigL.Address = (uint32_t)audioBufferL;
    dmaConfigL.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfigL.MsbOnly = DISABLE;
    if (HAL_MDF_AcqStart_DMA(&MdfHandle0, &MdfFilterConfig0, &dmaConfigL) != HAL_OK) {
        rtt_printf(0, "MDF1 Filter0 (L) DMA start FAILED\r\n");
        Error_Handler();
    }

    MDF_DmaConfigTypeDef dmaConfigR = {0};
    dmaConfigR.Address = (uint32_t)audioBufferR;
    dmaConfigR.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfigR.MsbOnly = DISABLE;
    if (HAL_MDF_AcqStart_DMA(&MdfHandle1, &MdfFilterConfig1, &dmaConfigR) != HAL_OK) {
        rtt_printf(0, "MDF1 Filter1 (R) DMA start FAILED\r\n");
        Error_Handler();
    }

    /* Fire TRGO so both filters start sample-aligned */
    HAL_MDF_GenerateTrgo(&MdfHandle0);

    rtt_printf(0, "MDF1: Stereo running (48kHz, Sinc4, gain=%d, L=rising, R=falling)\r\n",
               MdfFilterConfig0.Gain);

    uint32_t lastPrint = 0;

    while (1) {
        uint32_t pL = peakL;
        uint32_t pR = peakR;

        /* Exponential smoothing for stable LED + bars */
        smoothedL = (smoothedL * 3 + pL) / 4;
        smoothedR = (smoothedR * 3 + pR) / 4;

        /* LED brightness from MAX(L, R) */
        uint32_t maxPeak = (smoothedL > smoothedR) ? smoothedL : smoothedR;
        uint32_t duty = maxPeak * 999 / 32768;
        if (duty > 999) duty = 999;
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, duty);

        /* Print L/R bars at 10 Hz */
        uint32_t now = HAL_GetTick();
        if (now - lastPrint >= 100) {
            lastPrint = now;

            char barL[17], barR[17];
            uint32_t bL = smoothedL * 16 / 32768;
            uint32_t bR = smoothedR * 16 / 32768;
            if (bL > 16) bL = 16;
            if (bR > 16) bR = 16;
            for (uint32_t i = 0; i < 16; i++) {
                barL[i] = (i < bL) ? '#' : '.';
                barR[i] = (i < bR) ? '#' : '.';
            }
            barL[16] = barR[16] = '\0';

            rtt_printf(0, "L:%5lu [%s]   R:%5lu [%s]   PWM:%3lu\r\n",
                       (unsigned long)smoothedL, barL,
                       (unsigned long)smoothedR, barR,
                       (unsigned long)duty);
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
