/**
 * @file    dds.h
 * @brief   DDS Signal Generator — DAC output via DMA + TIM6.
 *
 * Architecture:
 *   32-bit phase accumulator → 256-point LUT → DAC (12-bit)
 *   TIM6 triggers DAC at 1 MHz update rate.
 *   DMA feeds DAC from the waveform buffer.
 */

#ifndef __DDS_H
#define __DDS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */

#define DDS_LUT_SIZE        256
#define DDS_PHASE_MAX       0x100000000ULL  /* 2^32 */
#define DDS_DAC_MAX         4095
#define DDS_DAC_MID         2047

/* ── API ──────────────────────────────────────────────────────────────── */

/**
 * @brief  Initialize DDS: sine LUT, DAC, TIM6, DMA.
 *         DAC output on PA4. TIM6 triggers at 1 MHz.
 */
void DDS_Init(void);

/**
 * @brief  Start signal output.
 */
void DDS_Start(void);

/**
 * @brief  Stop signal output (DAC holds last value).
 */
void DDS_Stop(void);

/**
 * @brief  Set output waveform type.
 * @param  wave: 0=sine, 1=square, 2=triangle, 3=sawtooth
 */
void DDS_SetWaveform(uint8_t wave);

/**
 * @brief  Set output frequency in Hz (1 – 100000).
 *         Updates the DDS phase increment.
 */
void DDS_SetFrequency(uint32_t freq_hz);

/**
 * @brief  Set output amplitude in mV (0 – 3300).
 *         Scales the LUT values around the midpoint.
 */
void DDS_SetAmplitude(uint16_t amp_mv);

/**
 * @brief  Get current DDS phase accumulator value.
 */
uint32_t DDS_GetPhase(void);

/**
 * @brief  TIM6 interrupt handler — call from TIM6_DAC_IRQHandler().
 */
void DDS_TIM6_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __DDS_H */
