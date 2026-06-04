/**
 * @file    usbd_conf.h
 * @brief   USB Device Configuration — Header
 */

#ifndef __USBD_CONF_H__
#define __USBD_CONF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"

/* ── USB Configuration ─────────────────────────────────────────────────── */

#define USBD_MAX_NUM_INTERFACES      1U
#define USBD_MAX_NUM_CONFIGURATION   1U
#define USBD_MAX_STR_DESC_SIZ        100U
#define USBD_DEBUG_LEVEL             0U
#define USBD_LPM_ENABLED             0U
#define USBD_SELF_POWERED            1U

#define DEVICE_FS                    0

/* ── Memory Management ──────────────────────────────────────────────────── */

#define USBD_malloc     (void *)USBD_static_malloc
#define USBD_free       USBD_static_free
#define USBD_memset     memset
#define USBD_memcpy     memcpy
#define USBD_Delay      HAL_Delay

/* ── Debug ──────────────────────────────────────────────────────────────── */

#if (USBD_DEBUG_LEVEL > 0)
#define USBD_UsrLog(...)    do { printf(__VA_ARGS__); printf("\n"); } while(0)
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...)    do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while(0)
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...)    do { printf("DEBUG: "); printf(__VA_ARGS__); printf("\n"); } while(0)
#else
#define USBD_DbgLog(...)
#endif

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_H__ */
