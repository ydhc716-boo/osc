/**
 * @file    scope.h
 * @brief   Oscilloscope — ADC sampling with DMA double-buffering.
 *
 * Architecture:
 *   ADC1 triggered by TIM2 at configurable rate.
 *   DMA double-buffer (buffer A + buffer B) for continuous acquisition.
 *   Half-transfer and full-transfer interrupts signal data readiness.
 *   Trigger detection: auto (continuous), rising edge, falling edge.
 */

#ifndef __SCOPE_H
#define __SCOPE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */

#define SCOPE_BUF_SIZE      1024    /* Total double-buffer size */
#define SCOPE_HALF_BUF      512     /* Each half size */
#define SCOPE_ADC_MAX       4095
#define SCOPE_VREF_MV       3300

/* ── Types ─────────────────────────────────────────────────────────────── */

typedef enum {
    SCOPE_TRIG_AUTO = 0,
    SCOPE_TRIG_RISING = 1,
    SCOPE_TRIG_FALLING = 2,
} ScopeTriggerMode;

typedef struct {
    uint16_t *ready_buf;     /* Pointer to the buffer half that's ready */
    uint16_t  ready_count;   /* Number of samples in ready_buf */
    uint16_t  seq;           /* Packet sequence number */
} ScopeData;

/* ── API ──────────────────────────────────────────────────────────────── */

/**
 * @brief  Initialize ADC, TIM2, and DMA for scope operation.
 *         ADC1 CH1 on PA0. TIM2 triggers ADC.
 */
void Scope_Init(void);

/**
 * @brief  Start continuous acquisition.
 */
void Scope_Start(void);

/**
 * @brief  Stop acquisition.
 */
void Scope_Stop(void);

/**
 * @brief  Set sample rate (1000 – 1000000 SPS).
 */
void Scope_SetSampleRate(uint32_t rate_hz);

/**
 * @brief  Set trigger mode and level.
 * @param  mode: 0=auto, 1=rising, 2=falling
 * @param  level_mv: trigger level in mV (0-3300)
 */
void Scope_SetTrigger(uint8_t mode, uint16_t level_mv);

/**
 * @brief  Check if a data buffer is ready. Returns NULL if none ready.
 *         Caller must copy data out before next buffer fills.
 */
const ScopeData *Scope_GetData(void);

/**
 * @brief  Mark the current buffer as consumed (ready for next acquisition).
 */
void Scope_DataConsumed(void);

/**
 * @brief  DMA half-transfer callback (called from ISR).
 */
void Scope_HalfCpltCallback(void);

/**
 * @brief  DMA full-transfer callback (called from ISR).
 */
void Scope_FullCpltCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCOPE_H */
