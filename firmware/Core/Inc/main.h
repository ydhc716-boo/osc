/**
 * @file    main.h
 * @brief   SCO firmware main header — STM32G474VET6.
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

/* ── Pin Definitions ──────────────────────────────────────────────────── */

/* DAC1 Channel 1 — Signal Generator Output */
#define SG_DAC              DAC1
#define SG_DAC_CHANNEL      DAC_CHANNEL_1
#define SG_DAC_GPIO_PORT    GPIOA
#define SG_DAC_GPIO_PIN     GPIO_PIN_4

/* ADC1 Channel 1 — Oscilloscope Input */
#define SCOPE_ADC           ADC1
#define SCOPE_ADC_CHANNEL   ADC_CHANNEL_1
#define SCOPE_ADC_GPIO_PORT GPIOA
#define SCOPE_ADC_GPIO_PIN  GPIO_PIN_0

/* USB OTG FS — CDC Communication */
/* PA11 = USB_DM, PA12 = USB_DP (fixed by hardware) */

/* Status LED (optional, on-board LED typically PC13 or PA5) */
#define LED_GPIO_PORT       GPIOA
#define LED_GPIO_PIN        GPIO_PIN_5

/* ── System Constants ─────────────────────────────────────────────────── */

#define SYSTEM_CLOCK_HZ     170000000UL
#define DDS_UPDATE_RATE_HZ  1000000UL   /* TIM6 triggers DAC at 1 MHz */
#define SCOPE_MAX_SPS       1000000UL   /* Max ADC sample rate */

/* ── Shared State ─────────────────────────────────────────────────────── */

typedef struct {
    /* Signal Generator */
    volatile uint8_t  sg_running;
    uint8_t           sg_waveform;    /* 0=sine, 1=square, 2=triangle, 3=sawtooth */
    uint32_t          sg_freq_hz;
    uint16_t          sg_amp_mv;
    uint32_t          sg_phase_acc;   /* 32-bit DDS phase accumulator */

    /* Oscilloscope */
    volatile uint8_t  scope_running;
    uint32_t          scope_sample_rate;
    uint8_t           scope_trigger_mode;
    uint16_t          scope_trigger_level;     /* ADC code (0-4095) */
    uint16_t          scope_trigger_level_mv;  /* trigger level in mV (0-3300) */
    uint16_t          scope_packet_size;

    /* Communication */
    volatile uint8_t  usb_connected;
    volatile uint8_t  data_ready;     /* ADC buffer ready to send */
} SystemState;

extern SystemState g_state;

/* External peripheral handles (defined in main.c, used by dds.c / scope.c) */
extern DAC_HandleTypeDef    hdac1;
extern ADC_HandleTypeDef    hadc1;
extern TIM_HandleTypeDef    htim2;
extern TIM_HandleTypeDef    htim6;
extern DMA_HandleTypeDef    hdma_dac1_ch1;
extern DMA_HandleTypeDef    hdma_adc1;

/* ── Exported Functions ────────────────────────────────────────────────── */

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_DAC_Init(void);
void MX_ADC_Init(void);
void MX_TIM6_Init(void);
void MX_TIM2_Init(void);
void MX_DMA_Init(void);
void MX_USB_CDC_Init(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
