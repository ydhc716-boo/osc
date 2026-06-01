/**
 * @file    scope.c
 * @brief   Oscilloscope — ADC1 double-buffered acquisition with TIM2 trigger.
 *
 * Architecture:
 *   - ADC1 CH1 (PA0) triggered by TIM2 OC1 at configurable rate
 *   - DMA double-buffer: adc_buf[0..1023] split into A[0..511] and B[512..1023]
 *   - DMA HT interrupt → buffer A ready; DMA TC interrupt → buffer B ready
 *   - Ready data passed to main loop for USB transmission
 */

#include "scope.h"
#include "main.h"
#include <string.h>

/* ── Static Data ───────────────────────────────────────────────────────── */

static uint16_t adc_buf[SCOPE_BUF_SIZE];  /* DMA double buffer */
static uint16_t ready_a[SCOPE_HALF_BUF];  /* Copy of buffer A when ready */
static uint16_t ready_b[SCOPE_HALF_BUF];  /* Copy of buffer B when ready */
static volatile uint8_t half_a_ready = 0;
static volatile uint8_t half_b_ready = 0;
static uint16_t packet_seq = 0;
static bool running = false;

static uint32_t current_sample_rate = 100000;
static uint8_t  current_trigger_mode = SCOPE_TRIG_AUTO;
static uint16_t current_trigger_level = 2047;  /* ADC mid-scale */

/* Timer clock: 170 MHz on APB1/APB2 timers */
#define TIM_CLOCK_HZ    170000000UL

/* ── Trigger Detection ─────────────────────────────────────────────────── */

/**
 * @brief  Check if trigger condition is met in a buffer.
 *         Returns index of first trigger point, or -1 if not found.
 */
static int Scope_FindTrigger(const uint16_t *buf, uint16_t count)
{
    if (current_trigger_mode == SCOPE_TRIG_AUTO) {
        return 0;  /* Always trigger at start */
    }

    for (uint16_t i = 1; i < count; i++) {
        uint16_t prev = buf[i - 1];
        uint16_t curr = buf[i];

        if (current_trigger_mode == SCOPE_TRIG_RISING) {
            if (prev < current_trigger_level && curr >= current_trigger_level) {
                return i;
            }
        } else { /* FALLING */
            if (prev > current_trigger_level && curr <= current_trigger_level) {
                return i;
            }
        }
    }
    return -1;
}

/* ── Public API ────────────────────────────────────────────────────────── */

void Scope_Init(void)
{
    memset(adc_buf, 0, sizeof(adc_buf));
    memset(ready_a, 0, sizeof(ready_a));
    memset(ready_b, 0, sizeof(ready_b));
    half_a_ready = 0;
    half_b_ready = 0;
    packet_seq = 0;

    /* Configure TIM2 for default rate */
    Scope_SetSampleRate(current_sample_rate);
}

void Scope_Start(void)
{
    running = true;
    half_a_ready = 0;
    half_b_ready = 0;
    packet_seq = 0;

    /* Reset and start ADC with DMA */
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, SCOPE_BUF_SIZE);

    /* Start TIM2 to trigger ADC */
    __HAL_TIM_ENABLE(&htim2);
}

void Scope_Stop(void)
{
    running = false;
    __HAL_TIM_DISABLE(&htim2);
    HAL_ADC_Stop_DMA(&hadc1);
}

void Scope_SetSampleRate(uint32_t rate_hz)
{
    if (rate_hz < 1000) rate_hz = 1000;
    if (rate_hz > SCOPE_MAX_SPS) rate_hz = SCOPE_MAX_SPS;
    current_sample_rate = rate_hz;

    /* TIM2 configuration: trigger ADC at the sample rate.
       TIM2 running at TIM_CLOCK_HZ.
       ARR = TIM_CLOCK / sample_rate - 1 */
    uint32_t arr = TIM_CLOCK_HZ / rate_hz - 1;

    /* Ensure ARR fits in 16-bit (TIM2 is 32-bit on G4, but safe) */
    if (arr > 0xFFFFFFFFUL) arr = 0xFFFFFFFFUL;
    if (arr < 1) arr = 1;

    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, arr / 2);  /* 50% duty */

    /* Update prescaler if needed for very low rates */
    uint32_t psc = 0;
    while (arr > 0xFFFF && psc < 0xFFFF) {
        psc++;
        arr = (TIM_CLOCK_HZ / (psc + 1)) / rate_hz - 1;
    }
    __HAL_TIM_SET_PRESCALER(&htim2, psc);
    if (arr > 0xFFFF) arr = 0xFFFF;
    __HAL_TIM_SET_AUTORELOAD(&htim2, (uint32_t)arr);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)arr / 2);
    __HAL_TIM_GENERATE_EVENT(&htim2, TIM_EVENTSOURCE_UPDATE);
}

void Scope_SetTrigger(uint8_t mode, uint16_t level_mv)
{
    if (mode > 2) mode = SCOPE_TRIG_AUTO;
    current_trigger_mode = mode;

    /* Convert mV to ADC code */
    uint32_t level = (uint32_t)level_mv * SCOPE_ADC_MAX / SCOPE_VREF_MV;
    if (level > SCOPE_ADC_MAX) level = SCOPE_ADC_MAX;
    current_trigger_level = (uint16_t)level;
}

const ScopeData *Scope_GetData(void)
{
    static ScopeData data;

    if (half_a_ready) {
        /* Copy buffer A to ready area with trigger alignment */
        int trig_idx = Scope_FindTrigger(adc_buf, SCOPE_HALF_BUF);
        if (trig_idx < 0) {
            /* No trigger found — use the whole buffer */
            memcpy(ready_a, adc_buf, SCOPE_HALF_BUF * sizeof(uint16_t));
            data.ready_buf = ready_a;
            data.ready_count = SCOPE_HALF_BUF;
        } else {
            /* Align: samples from trig_idx to end, then wrap */
            uint16_t count = SCOPE_HALF_BUF - trig_idx;
            memcpy(ready_a, &adc_buf[trig_idx], count * sizeof(uint16_t));
            if (trig_idx > 0) {
                memcpy(&ready_a[count], adc_buf, trig_idx * sizeof(uint16_t));
            }
            data.ready_buf = ready_a;
            data.ready_count = SCOPE_HALF_BUF;
        }
        half_a_ready = 0;
        data.seq = packet_seq++;
        return &data;
    }

    if (half_b_ready) {
        int trig_idx = Scope_FindTrigger(&adc_buf[SCOPE_HALF_BUF], SCOPE_HALF_BUF);
        if (trig_idx < 0) {
            memcpy(ready_b, &adc_buf[SCOPE_HALF_BUF], SCOPE_HALF_BUF * sizeof(uint16_t));
            data.ready_buf = ready_b;
            data.ready_count = SCOPE_HALF_BUF;
        } else {
            uint16_t count = SCOPE_HALF_BUF - trig_idx;
            memcpy(ready_b, &adc_buf[SCOPE_HALF_BUF + trig_idx], count * sizeof(uint16_t));
            if (trig_idx > 0) {
                memcpy(&ready_b[count], &adc_buf[SCOPE_HALF_BUF], trig_idx * sizeof(uint16_t));
            }
            data.ready_buf = ready_b;
            data.ready_count = SCOPE_HALF_BUF;
        }
        half_b_ready = 0;
        data.seq = packet_seq++;
        return &data;
    }

    return NULL;
}

void Scope_DataConsumed(void)
{
    /* Data has been copied by caller — nothing to do,
       the half_x_ready flags are already cleared in Scope_GetData(). */
}

/* ── DMA Callbacks ─────────────────────────────────────────────────────── */

void Scope_HalfCpltCallback(void)
{
    /* First half of buffer (adc_buf[0..511]) is ready */
    if (running) {
        half_a_ready = 1;
    }
}

void Scope_FullCpltCallback(void)
{
    /* Second half of buffer (adc_buf[512..1023]) is ready */
    if (running) {
        half_b_ready = 1;
    }
}

/* ── ADC DMA ISR Handlers ────────────────────────────────────────────────

   These are called from the main HAL DMA interrupt handlers in main.c.
   We use the HAL callback mechanism.

   In STM32CubeMX-generated code, these would be in the appropriate
   interrupt handler files. Here we define the HAL callback overrides.
   ─────────────────────────────────────────────────────────────────────── */

/**
 * @brief  Override HAL ADC DMA half-transfer callback.
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        Scope_HalfCpltCallback();
    }
}

/**
 * @brief  Override HAL ADC DMA full-transfer callback.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        Scope_FullCpltCallback();
    }
}
