/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32u5xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32u5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "SEGGER_RTT.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void hf_puts(const char *s) {
    SEGGER_RTT_Write(0, s, strlen(s));
}
static void hf_hex32(uint32_t v) {
    const char hex[] = "0123456789ABCDEF";
    char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = hex[(v >> (28 - i * 4)) & 0xF];
    SEGGER_RTT_Write(0, buf, 8);
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_NodeTypeDef Node_GPDMA1_Channel0;
extern DMA_QListTypeDef List_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern TIM_HandleTypeDef htim17;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  {
    volatile uint32_t cfsr  = SCB->CFSR;
    volatile uint32_t hfsr  = SCB->HFSR;
    volatile uint32_t mmfar = SCB->MMFAR;
    volatile uint32_t bfar  = SCB->BFAR;

    /* Get the stacked frame from PSP or MSP depending on EXC_RETURN */
    uint32_t *frame;
    __asm volatile ("tst lr, #4 \n"
                    "ite eq     \n"
                    "mrseq %0, msp \n"
                    "mrsne %0, psp \n"
                    : "=r" (frame));
    /* frame: [0]=R0 [1]=R1 [2]=R2 [3]=R3 [4]=R12 [5]=LR [6]=PC [7]=xPSR */

    hf_puts("\r\n!!! HARDFAULT\r\n");
    hf_puts("PC=");  hf_hex32(frame[6]); hf_puts("\r\n");
    hf_puts("LR=");  hf_hex32(frame[5]); hf_puts("\r\n");
    hf_puts("SP=");  hf_hex32((uint32_t)frame); hf_puts("\r\n");
    hf_puts("CFSR="); hf_hex32(cfsr); hf_puts("\r\n");
    hf_puts("HFSR="); hf_hex32(hfsr); hf_puts("\r\n");
    hf_puts("BFAR="); hf_hex32(bfar); hf_puts("\r\n");
    hf_puts("MMFAR="); hf_hex32(mmfar); hf_puts("\r\n");
    hf_puts("R0="); hf_hex32(frame[0]); hf_puts(" R1="); hf_hex32(frame[1]); hf_puts("\r\n");
    hf_puts("R2="); hf_hex32(frame[2]); hf_puts(" R3="); hf_hex32(frame[3]); hf_puts("\r\n");
    hf_puts("R12="); hf_hex32(frame[4]); hf_puts("\r\n");
  }
  /* Rapid-blink status LED */
  for (;;) {
    GPIOD->ODR ^= GPIO_PIN_13;
    for (volatile int i = 0; i < 500000; i++) {}
  }
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  GPIOD->BSRR = GPIO_PIN_13; /* status LED on (PD13) */
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  GPIOD->BSRR = GPIO_PIN_13; /* status LED on (PD13) */
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  GPIOD->BSRR = GPIO_PIN_13; /* status LED on (PD13) */
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32U5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32u5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 0 */

  /* USER CODE END GPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 1 */

  /* USER CODE END GPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles TIM17 global interrupt.
  */
void TIM17_IRQHandler(void)
{
  /* USER CODE BEGIN TIM17_IRQn 0 */

  /* USER CODE END TIM17_IRQn 0 */
  HAL_TIM_IRQHandler(&htim17);
  /* USER CODE BEGIN TIM17_IRQn 1 */

  /* USER CODE END TIM17_IRQn 1 */
}

/* USER CODE BEGIN 1 */
extern RTC_HandleTypeDef hrtc;

void RTC_IRQHandler(void)
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}

void EXTI0_IRQHandler(void)
{
    /* ESP32 CS wake — clear pending, wake source checked in enterStop2() */
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI8_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
}

void USART1_IRQHandler(void)
{
    if (USART1->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t ch = (uint8_t)(USART1->RDR & 0xFF);
        extern osMessageQueueId_t gpsRxQueue;
        osMessageQueuePut(gpsRxQueue, &ch, 0, 0);
    }
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
}

void USART3_IRQHandler(void)
{
    if (USART3->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t ch = (uint8_t)(USART3->RDR & 0xFF);
        extern osMessageQueueId_t consoleRxQueue;
        osMessageQueuePut(consoleRxQueue, &ch, 0, 0);
    }
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
}
/* USER CODE END 1 */
