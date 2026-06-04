/**
 * @file    usb_device.c
 * @brief   USB Device initialization — SCO oscilloscope.
 *
 * Adapted from STM32G474E-EVAL CDC_Standalone example.
 */

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

/* USB Device Core handle */
USBD_HandleTypeDef hUsbDeviceFS;

/* External CDC descriptor (defined in usbd_desc.c) */
extern USBD_DescriptorsTypeDef CDC_Desc;

/**
 * @brief  Initialize USB HSI48 clock with CRS synchronization.
 */
void USBD_Clock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

    /* Enable HSI48 for USB (48 MHz) */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Select HSI48 as USB clock source using direct register write
       to avoid potential HAL conflicts with already-configured system clock. */
    RCC->CCIPR &= ~RCC_CCIPR_CLK48SEL;  /* CLK48SEL = 0 → HSI48 */

    /* Configure CRS (Clock Recovery System) to trim HSI48 from USB SOF */
    __HAL_RCC_CRS_CLK_ENABLE();

    RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
    RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
    RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
    RCC_CRSInitStruct.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
    RCC_CRSInitStruct.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;

    HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

/**
 * @brief  Initialize USB Device CDC stack.
 */
void MX_USB_DEVICE_Init(void)
{
    /* Initialize USB clock (HSI48 + CRS) */
    USBD_Clock_Config();

    /* Init Device Library */
    if (USBD_Init(&hUsbDeviceFS, &CDC_Desc, DEVICE_FS) != USBD_OK) {
        Error_Handler();
    }

    /* Register CDC class */
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK) {
        Error_Handler();
    }

    /* Register CDC interface callbacks */
    if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_CDC_fops_FS) != USBD_OK) {
        Error_Handler();
    }

    /* Start USB Device */
    if (USBD_Start(&hUsbDeviceFS) != USBD_OK) {
        Error_Handler();
    }
}
