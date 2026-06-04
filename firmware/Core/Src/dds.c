/**
 * @file    dds.c
 * @brief   DDS Signal Generator implementation.
 *
 * Uses TIM6 to trigger DAC1 at 1 MHz. DMA moves waveform samples
 * from a buffer to the DAC data register. The DDS core updates
 * the buffer content based on phase accumulator + LUT.
 *
 * For sine wave: pre-computed 256-point LUT indexed by top 8 bits of phase.
 * For square/triangle/sawtooth: computed directly each update cycle.
 */

#include "dds.h"
#include "main.h"
#include <math.h>
#include <string.h>

/* ── Static Data ───────────────────────────────────────────────────────── */

static uint16_t sine_lut[DDS_LUT_SIZE];
static uint16_t wave_buf[DDS_LUT_SIZE];  /* DMA source buffer */
static uint32_t phase_increment;          /* DDS phase step */
static uint8_t  current_waveform;
static uint16_t current_amplitude = 3300;
static bool     running = false;


/* ── Sine LUT Generation ───────────────────────────────────────────────── */

static void DDS_GenerateSineLUT(void)
{
    for (int i = 0; i < DDS_LUT_SIZE; i++) {
        double phase = 2.0 * 3.141592653589793 * i / DDS_LUT_SIZE;
        double val = sin(phase);
        /* Scale to 0–4095 with mid-point 2047 */
        sine_lut[i] = (uint16_t)(DDS_DAC_MID + (int16_t)(DDS_DAC_MID * val));
    }
}

/* ── Waveform Buffer Update ────────────────────────────────────────────── */

/**
 * @brief  Fill wave_buf[] with one cycle of the current waveform at
 *         full amplitude. The DDS uses phase to index into this.
 *         For non-sine waveforms, we generate 256 points directly.
 */
static void DDS_UpdateWaveBuffer(void)
{
    switch (current_waveform) {
    case 0: /* Sine — copy from LUT */
        memcpy(wave_buf, sine_lut, sizeof(sine_lut));
        break;

    case 1: /* Square */
        for (int i = 0; i < DDS_LUT_SIZE; i++) {
            wave_buf[i] = (i < DDS_LUT_SIZE / 2) ? DDS_DAC_MAX : 0;
        }
        break;

    case 2: /* Triangle */
        for (int i = 0; i < DDS_LUT_SIZE; i++) {
            if (i < DDS_LUT_SIZE / 2) {
                wave_buf[i] = (uint16_t)((uint32_t)i * DDS_DAC_MAX * 2 / DDS_LUT_SIZE);
            } else {
                wave_buf[i] = (uint16_t)(DDS_DAC_MAX - (uint32_t)(i - DDS_LUT_SIZE/2) * DDS_DAC_MAX * 2 / DDS_LUT_SIZE);
            }
        }
        break;

    case 3: /* Sawtooth */
        for (int i = 0; i < DDS_LUT_SIZE; i++) {
            wave_buf[i] = (uint16_t)((uint32_t)i * DDS_DAC_MAX / DDS_LUT_SIZE);
        }
        break;
    }

    /* Apply amplitude scaling (scale around midpoint) */
    if (current_amplitude < 3300) {
        float scale = (float)current_amplitude / 3300.0f;
        for (int i = 0; i < DDS_LUT_SIZE; i++) {
            int16_t centered = (int16_t)(wave_buf[i] - DDS_DAC_MID);
            int16_t scaled = (int16_t)(centered * scale);
            int32_t result = DDS_DAC_MID + scaled;
            if (result < 0) result = 0;
            if (result > DDS_DAC_MAX) result = DDS_DAC_MAX;
            wave_buf[i] = (uint16_t)result;
        }
    }
}

/* ── Phase Increment Calculation ───────────────────────────────────────── */

static void DDS_UpdatePhaseIncrement(void)
{
    /* phase_inc = freq * 2^32 / Fs
       Fs = 1,000,000 Hz (TIM6 trigger rate) */
    uint64_t inc = ((uint64_t)g_state.sg_freq_hz << 32) / DDS_UPDATE_RATE_HZ;
    phase_increment = (uint32_t)inc;
}

/* ── High-Frequency Output Mode ──────────────────────────────────────────

   For best performance at high frequencies, we use a different approach:
   Instead of updating the DMA buffer continuously, we pre-fill a larger
   buffer that represents the waveform at the desired frequency and let
   the DMA circularly read from it.

   For frequencies where an integer number of samples fits in the buffer,
   this gives jitter-free output.

   Simplified approach: Use a 256-sample buffer. At each TIM6 tick,
   DMA reads the next sample. We use a circular DMA mode and update
   the buffer content asynchronously when frequency/amplitude changes.

   The key insight: with a 256-sample buffer and 1MHz update rate,
   the buffer repeats at 1MHz/256 ≈ 3.9 kHz. For frequencies below
   ~3.9 kHz, we need a larger effective buffer. We handle this by
   using the 32-bit phase accumulator to index into the 256-point
   LUT — the phase wraps at the desired frequency regardless of
   the buffer repetition rate.

   Actually, the simplest approach: the DMA circular buffer is just
   a 1-sample value that we update in the TIM6 ISR based on the
   phase accumulator. This gives us true DDS with maximum flexibility.

   Let's use a hybrid: a small buffer (e.g., 32 samples) that we
   fill repeatedly. The TIM6 triggers DAC, DMA reads from the buffer
   circularly. In the main loop or a timer ISR, we refill the buffer
   with the next batch of DDS samples.
   ─────────────────────────────────────────────────────────────────────── */

/* For simplicity, use a larger pre-computed buffer approach:
   Generate `BUF_SIZE` samples of the DDS waveform, let DMA play
   them circularly. Resolution: BUF_SIZE samples per waveform period
   at the target frequency.

   BUF_SIZE = 256, Fs = 1MHz. For a 1kHz sine, one period = 1000 samples.
   With 256-sample buffer, the phase discontinuity at wraparound could
   cause glitches.

   Solution: Use 32-bit phase accumulator. In TIM6 ISR (or DMA HT/TC ISR),
   compute the next sample from the phase acc and update a small ping-pong
   buffer.

   FINAL APPROACH (simplest, cleanest):
   - Single-value DMA: DMA reads one uint16 from memory to DAC_DHR12R1
   - TIM6 triggers both DAC and a DMA request
   - In DMA HT/TC interrupt, compute next sample and update the buffer
   - Uses circular DMA with buffer size = 2 (ping-pong)
   - Actually, just use a buffer of 2 samples: while DMA outputs one,
     we compute the other. This is essentially software DDS at 1MHz.
   - At 170MHz CPU, we have 170 cycles per sample — plenty.
   ─────────────────────────────────────────────────────────────────────── */

/*
 * Simplified implementation:
 *   Circular DMA buffer of DDS_LUT_SIZE (256) samples.
 *   Buffer pre-filled with the full waveform based on phase accumulator.
 *   DMA reads sequentially; buffer wraps circularly.
 *   For high frequencies (> ~4kHz), the buffer contains multiple cycles.
 *   For low frequencies, we need a much larger buffer or live updating.
 *
 * COMPROMISE: Use a single-value DMA. In each DMA transfer complete ISR,
 * compute the next DDS sample and write it. This is the most flexible.
 *
 * BUT: DMA transfer-complete interrupt at 1 MHz = 1M interrupts/sec.
 * At 170 MHz, that's 170 cycles per interrupt. Workable but tight.
 *
 * BETTER: Use the TIM6 update ISR directly (no DMA for DAC).
 * In the ISR: compute phase_inc, index LUT, write DAC->DHR12R1.
 * 1M ISR/sec at 170 MHz — each ISR must be < 170 cycles.
 * This is achievable with optimized code.
 *
 * EVEN BETTER: Use a larger DMA buffer (e.g. 256 samples) and let it
 * loop. The 256-sample buffer represents one cycle of the waveform
 * (for any frequency). The phase accumulator selects which sample
 * to output. Wait, that's not how DMA works — DMA always outputs
 * sequential samples from the buffer.
 *
 * OK, FINAL APPROACH:
 *   1. Use DMA circular mode, buffer size = 256
 *   2. At startup and on parameter change, fill the buffer with DDS
 *      samples that represent the waveform at the desired frequency
 *   3. The buffer contains an INTEGER number of waveform cycles
 *   4. When buffer wraps, the phase is continuous (by design)
 *
 *   Generate N*Fs/freq points where N is chosen such that
 *   N * Fs / freq is close to 256 and an integer.
 *
 *   Actually this is complicated. Let me use the simplest approach
 *   that works: a 1-sample DMA buffer.
 *   In the DMA TC ISR, compute next sample from phase acc and update.
 *   Write directly to DAC_DHR12R1 via DMA.
 *
 *   Even simpler: Don't use DMA at all.
 *   In TIM6 ISR: compute sample from phase acc, write to DAC_DHR12R1.
 *   Direct register write — fast and simple.
 *   - Load phase acc
 *   - Add increment
 *   - Extract top 8 bits
 *   - Index LUT
 *   - Write DAC register
 *   This is maybe 10-15 instructions. At 170 MHz that's < 0.1 µs.
 *   1 MHz ISR rate = 1 µs between interrupts. Very feasible.
 */

/* We take the TIM6 ISR approach for simplicity and flexibility. */

/* ── Public API ────────────────────────────────────────────────────────── */

void DDS_Init(void)
{
    DDS_GenerateSineLUT();
    current_waveform = 0;
    DDS_UpdateWaveBuffer();
    DDS_UpdatePhaseIncrement();
}

void DDS_Start(void)
{
    g_state.sg_phase_acc = 0;
    running = true;

    /* Start TIM6 with update interrupt (DDS core runs in ISR) */
    HAL_TIM_Base_Start_IT(&htim6);

    /* Enable DAC */
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
}

void DDS_Stop(void)
{
    running = false;
    HAL_TIM_Base_Stop_IT(&htim6);
    HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);

    /* Set output to 0V (mid-scale for AC-coupled, 0 for DC) */
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0);
}

void DDS_SetWaveform(uint8_t wave)
{
    if (wave > 3) return;
    current_waveform = wave;
    DDS_UpdateWaveBuffer();
}

void DDS_SetFrequency(uint32_t freq_hz)
{
    if (freq_hz < 1) freq_hz = 1;
    if (freq_hz > 100000) freq_hz = 100000;
    g_state.sg_freq_hz = freq_hz;
    DDS_UpdatePhaseIncrement();
}

void DDS_SetAmplitude(uint16_t amp_mv)
{
    if (amp_mv > 3300) amp_mv = 3300;
    current_amplitude = amp_mv;
    DDS_UpdateWaveBuffer();
}

uint32_t DDS_GetPhase(void)
{
    return g_state.sg_phase_acc;
}

/* ── TIM6 ISR — DDS Core ──────────────────────────────────────────────── */

/**
 * @brief  TIM6 update interrupt handler (called at 1 MHz).
 *         Computes next DDS sample and writes to DAC.
 *
 *         This ISR must be fast: ~10-15 CPU cycles target.
 *         At 170 MHz, 1 µs between interrupts → ~170 cycles budget.
 */
void DDS_TIM6_IRQHandler(void)
{
    if (!running) {
        /* Should not happen if TIM6 is disabled, but guard */
        return;
    }

    /* Clear TIM6 update flag */
    __HAL_TIM_CLEAR_IT(&htim6, TIM_IT_UPDATE);

    /* DDS phase accumulator update */
    uint32_t phase = g_state.sg_phase_acc;
    phase += phase_increment;
    g_state.sg_phase_acc = phase;

    /* Extract top 8 bits for LUT index */
    uint8_t idx = (uint8_t)(phase >> 24);

    /* Write to DAC data register */
    DAC1->DHR12R1 = wave_buf[idx];
}
