/**
 * @file    stm32g4xx_it.c
 * @brief   Cortex-M4 Processor Exceptions Handlers.
 *
 * Peripheral interrupt handlers (TIM6_DAC, DMA1_Channel1, USB_LP)
 * are defined in main.c alongside their respective initialization code.
 */

#include "main.h"

/* ── Cortex-M4 Processor Exceptions ─────────────────────────────────────── */

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
