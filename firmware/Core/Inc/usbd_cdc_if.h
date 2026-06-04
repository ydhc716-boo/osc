/**
 * @file    usbd_cdc_if.h
 * @brief   Header for usbd_cdc_if.c
 */

#ifndef __USBD_CDC_IF_H__
#define __USBD_CDC_IF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_cdc.h"

#define APP_RX_DATA_SIZE    2048
#define APP_TX_DATA_SIZE    2048

extern USBD_CDC_ItfTypeDef USBD_CDC_fops_FS;
extern USBD_CDC_LineCodingTypeDef LineCoding;

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_H__ */
