/**
 * @file    stm32g4xx_it.c
 * @brief   Interrupt Service Routines.
 *
 * This file is a TEMPLATE. CubeMX generates the real version with all
 * required IRQ handlers. Copy the relevant handlers from main.c ISR
 * section into the generated file.
 */

#include "main.h"

/* External variables from main.c */
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim6;
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_dac1_ch1;

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void) {}

void HardFault_Handler(void)
{
    while (1);
}

void MemManage_Handler(void)
{
    while (1);
}

void BusFault_Handler(void)
{
    while (1);
}

void UsageFault_Handler(void)
{
    while (1);
}

void SVC_Handler(void) {}

void DebugMon_Handler(void) {}

void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/******************************************************************************/
/*            STM32G4xx Peripheral Interrupt Handlers                         */
/******************************************************************************/

/**
 * @brief  TIM6 and DAC global interrupt.
 *         TIM6 update triggers DAC. We compute next DDS sample here.
 */
void TIM6_DAC_IRQHandler(void)
{
    extern void DDS_TIM6_IRQHandler(void);
    DDS_TIM6_IRQHandler();
}

/**
 * @brief  DMA1 Channel 1 interrupt (ADC1).
 */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

/**
 * @brief  USB Low Priority interrupt.
 */
void USB_LP_IRQHandler(void)
{
    extern USBD_HandleTypeDef hUsbDeviceFS;
    HAL_PCD_IRQHandler((PCD_HandleTypeDef *)hUsbDeviceFS.pData);
}

/**
 * @brief  USB Wakeup interrupt (optional).
 */
void USBWakeUp_IRQHandler(void)
{
    /* Handled by HAL */
}
