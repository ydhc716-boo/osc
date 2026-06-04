/**
 * @file    usbd_cdc_if.c
 * @brief   USB CDC ACM interface — bridge between USB stack and SCO application.
 *
 * Receives protocol packets via CDC and passes them to main loop.
 * Transmits responses and scope data via USB_Send() in main.c.
 */

#include "main.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include <string.h>

/* ── USB CDC Structures ────────────────────────────────────────────────── */

/* USB Device Core handle (defined in usb_device.c) */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* CDC Line Coding (default: 115200 8N1) */
USBD_CDC_LineCodingTypeDef LineCoding = {
    .bitrate    = 115200,
    .format     = 0x00,  /* 1 stop bit */
    .paritytype = 0x00,  /* None */
    .datatype   = 0x08,  /* 8 data bits */
};

/* CDC receive buffer */
static uint8_t UserRxBuffer[APP_RX_DATA_SIZE];

/* ── External callback (defined in main.c) ─────────────────────────────── */

extern void CDC_Receive_Callback(uint8_t *buf, uint32_t len);

/* ── CDC Callbacks ──────────────────────────────────────────────────────── */

/**
 * @brief  Initialize CDC interface (FS).
 */
static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, NULL, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBuffer);
    return USBD_OK;
}

/**
 * @brief  De-initialize CDC interface (FS).
 */
static int8_t CDC_DeInit_FS(void)
{
    return USBD_OK;
}

/**
 * @brief  CDC control requests (line coding, etc.).
 */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        memcpy(&LineCoding, pbuf, sizeof(LineCoding));
        break;
    case CDC_GET_LINE_CODING:
        memcpy(pbuf, &LineCoding, sizeof(LineCoding));
        break;
    case CDC_SET_CONTROL_LINE_STATE:
        break;
    default:
        break;
    }
    return USBD_OK;
}

/**
 * @brief  Called when USB CDC receives data from host.
 *         Forward to application callback (main.c).
 */
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    CDC_Receive_Callback(Buf, *Len);

    /* Re-arm reception */
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBuffer);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);

    return USBD_OK;
}

/**
 * @brief  Transmit complete callback (unused for now).
 */
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
    UNUSED(pbuf);
    UNUSED(Len);
    UNUSED(epnum);
    return USBD_OK;
}

/**
 * @brief  Send data over USB CDC (used by main.c USB_Send).
 */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL) return USBD_BUSY;
    if (hcdc->TxState != 0) {
        return USBD_BUSY;
    }
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}

/* ── CDC Interface Callbacks Structure ──────────────────────────────────── */

USBD_CDC_ItfTypeDef USBD_CDC_fops_FS = {
    .Init       = CDC_Init_FS,
    .DeInit     = CDC_DeInit_FS,
    .Control    = CDC_Control_FS,
    .Receive    = CDC_Receive_FS,
    .TransmitCplt = CDC_TransmitCplt_FS,
};
