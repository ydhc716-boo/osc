/**
 * @file    main.c
 * @brief   SCO Firmware main entry point — STM32G474VET6.
 *
 * Signal Generator (DDS + DAC) + Oscilloscope (ADC + DMA) + USB CDC.
 *
 * Hardware:
 *   PA0  — ADC1_IN1  (Oscilloscope input)
 *   PA4  — DAC1_OUT1 (Signal generator output)
 *   PA5  — Status LED
 *   PA11 — USB_DM
 *   PA12 — USB_DP
 *
 * Communication: USB CDC virtual COM port, binary protocol.
 */

#include "main.h"
#include "dds.h"
#include "scope.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

/* ── Global State ──────────────────────────────────────────────────────── */

SystemState g_state = {
    .sg_running         = 0,
    .sg_waveform        = 0,
    .sg_freq_hz         = 1000,
    .sg_amp_mv          = 3300,
    .sg_phase_acc       = 0,
    .scope_running      = 0,
    .scope_sample_rate  = 100000,
    .scope_trigger_mode = 0,
    .scope_trigger_level= 2047,
    .scope_packet_size  = 512,
    .usb_connected      = 0,
    .data_ready         = 0,
};

/* ── Peripheral Handles ─────────────────────────────────────────────────── */

DAC_HandleTypeDef    hdac1;
ADC_HandleTypeDef    hadc1;
TIM_HandleTypeDef    htim2;
TIM_HandleTypeDef    htim6;
DMA_HandleTypeDef    hdma_dac1_ch1;
DMA_HandleTypeDef    hdma_adc1;

/* USBD handle (declared in usbd_cdc_if.c) */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* ── USB CDC Receive Buffer ─────────────────────────────────────────────── */

#define USB_RX_BUF_SIZE  1024
static uint8_t usb_rx_buf[USB_RX_BUF_SIZE];
static volatile uint16_t usb_rx_len = 0;
static ProtoParser proto_parser;

/* USB TX buffer */
#define USB_TX_BUF_SIZE  2048
static uint8_t usb_tx_buf[USB_TX_BUF_SIZE];

/* ── USB CDC Callbacks ──────────────────────────────────────────────────── */

/**
 * @brief  Called when USB CDC receives data from host.
 *         We buffer it and process in main loop.
 */
void CDC_Receive_Callback(uint8_t *buf, uint32_t len)
{
    if (len > 0 && (usb_rx_len + len) <= USB_RX_BUF_SIZE) {
        memcpy(&usb_rx_buf[usb_rx_len], buf, len);
        usb_rx_len += (uint16_t)len;
    }
}

/**
 * @brief  Send data to USB CDC host.
 *         Returns bytes sent or 0 on failure.
 */
static int USB_Send(const uint8_t *data, uint16_t len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL) return 0;

    /* Wait for previous TX to complete */
    uint32_t timeout = 100000;
    while (hcdc->TxState != 0 && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) return 0;

    /* Ensure we don't exceed buffer */
    if (len > USB_TX_BUF_SIZE) len = USB_TX_BUF_SIZE;

    memcpy(usb_tx_buf, data, len);
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, usb_tx_buf, len);

    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK) {
        return 0;
    }
    return len;
}

/* ── Command Processing ─────────────────────────────────────────────────── */

/**
 * @brief  Process one received command packet.
 */
static void ProcessCommand(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    uint8_t resp_buf[256];
    size_t resp_len = 0;

    switch (cmd) {

    case CMD_SET_WAVEFORM:
        if (len >= 1 && data[0] <= 3) {
            g_state.sg_waveform = data[0];
            DDS_SetWaveform(data[0]);
        }
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_SET_FREQ:
        if (len >= 4) {
            uint32_t freq = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            if (freq >= 1 && freq <= 100000) {
                g_state.sg_freq_hz = freq;
                DDS_SetFrequency(freq);
            }
        }
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_SET_AMPLITUDE:
        if (len >= 2) {
            uint16_t amp = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            if (amp <= 3300) {
                g_state.sg_amp_mv = amp;
                DDS_SetAmplitude(amp);
            }
        }
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_SET_SAMPLE_RATE:
        if (len >= 4) {
            uint32_t rate = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            if (rate >= 1000 && rate <= 1000000) {
                g_state.scope_sample_rate = rate;
                Scope_SetSampleRate(rate);
            }
        }
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_SET_TRIGGER:
        if (len >= 3) {
            uint8_t mode = data[0];
            uint16_t level = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
            if (mode <= 2 && level <= 3300) {
                g_state.scope_trigger_mode = mode;
                Scope_SetTrigger(mode, level);
            }
        }
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_START_SG:
        g_state.sg_running = 1;
        DDS_Start();
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_STOP_SG:
        g_state.sg_running = 0;
        DDS_Stop();
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_START_SCOPE:
        g_state.scope_running = 1;
        Scope_Start();
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_STOP_SCOPE:
        g_state.scope_running = 0;
        Scope_Stop();
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    case CMD_GET_STATUS:
        resp_len = proto_build_status(
            g_state.sg_running, g_state.scope_running,
            g_state.sg_waveform, g_state.sg_freq_hz, g_state.sg_amp_mv,
            g_state.scope_trigger_mode, g_state.scope_trigger_level_mv,
            g_state.scope_sample_rate, resp_buf, sizeof(resp_buf));
        break;

    default:
        resp_len = proto_build_error(ERR_BAD_CMD, resp_buf, sizeof(resp_buf));
        break;
    }

    if (resp_len > 0) {
        USB_Send(resp_buf, (uint16_t)resp_len);
    }
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    /* HAL Init */
    HAL_Init();
    SystemClock_Config();

    /* Peripheral Init */
    MX_GPIO_Init();
    MX_DAC_Init();
    MX_ADC_Init();
    MX_TIM6_Init();
    MX_TIM2_Init();
    MX_DMA_Init();

    /* DDS Init */
    DDS_Init();

    /* Scope Init */
    Scope_Init();

    /* Protocol Parser Init */
    proto_parser_init(&proto_parser);

    /* USB CDC Init */
    MX_USB_CDC_Init();

    /* Status LED — indicate ready */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_SET);

    /* ── Main Loop ──────────────────────────────────────────────────── */
    while (1) {
        /* ── Process USB received data ───────────────────────────────── */
        if (usb_rx_len > 0) {
            /* Feed bytes to protocol parser */
            for (uint16_t i = 0; i < usb_rx_len; i++) {
                if (proto_parser_feed(&proto_parser, usb_rx_buf[i])) {
                    /* Complete packet received */
                    ProcessCommand(proto_parser.cmd,
                                   proto_parser.data_buf,
                                   proto_parser.data_len);
                    proto_parser_init(&proto_parser);
                }
            }
            usb_rx_len = 0;
        }

        /* ── Stream scope data ──────────────────────────────────────── */
        if (g_state.scope_running) {
            const ScopeData *sd = Scope_GetData();
            if (sd != NULL && sd->ready_count > 0) {
                uint8_t tx_buf[PROTO_BUF_SIZE];
                size_t tx_len = proto_build_scope_data(
                    sd->seq, sd->ready_buf, sd->ready_count,
                    tx_buf, sizeof(tx_buf));
                if (tx_len > 0) {
                    USB_Send(tx_buf, (uint16_t)tx_len);
                }
                Scope_DataConsumed();
            }
        }

        /* ── LED heartbeat (optional) ────────────────────────────────── */
        static uint32_t led_tick = 0;
        led_tick++;
        if (led_tick > 500000) {
            HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
            led_tick = 0;
        }
    }
}

/* ── Peripheral Initialization ──────────────────────────────────────────── */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /** Configure the main internal regulator output voltage */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    /** Initializes the RCC Oscillators */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LED PA5 */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);
}

void MX_DAC_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_DAC1_CLK_ENABLE();

    hdac1.Instance = DAC1;
    HAL_DAC_Init(&hdac1);

    sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  /* TIM6 triggers DAC */
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);
}

void MX_ADC_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC12_CLK_ENABLE();

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation = 0;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T2_TRGO;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    HAL_ADC_Init(&hadc1);

    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

void MX_TIM6_Init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance = TIM6;
    htim6.Init.Prescaler = 0;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = 169;  /* 170 MHz / 170 = 1 MHz */
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);

    /* Configure TIM6 TRGO to trigger DAC */
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig);

    /* Enable TIM6 interrupt for DDS update */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 1699;  /* 170MHz / 1700 = 100kHz default */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim2);

    /* OC1 for ADC trigger */
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 849;  /* 50% duty */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);

    /* TRGO for ADC */
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_OC1;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig);
}

void MX_DMA_Init(void)
{
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* DMA for ADC1 */
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    /* DMA interrupt for ADC double buffering */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void MX_USB_CDC_Init(void)
{
    /* USB CDC initialization is handled by the STM32 USB Device library.
       In a real CubeMX project, this calls USBD_Init() and registers
       the CDC interface class.

       Key steps:
       1. Enable USB OTG FS clock
       2. Configure PA11 (DM) and PA12 (DP) in AF mode
       3. Initialize USB device stack with CDC ACM class
       4. Start USB device

       This function is a placeholder — actual implementation depends
       on CubeMX-generated USB middleware code.
       ───────────────────────────────────────────────────────────────── */

    /* Enable USB clock */
    __HAL_RCC_USB_CLK_ENABLE();

    /* GPIO PA11, PA12 for USB */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USB Pins PA9 (VBUS) — optional */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Enable USB FS IRQ */
    HAL_NVIC_SetPriority(USB_LP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_LP_IRQn);

    /* Note: Full USB CDC init requires CubeMX-generated usb_device.c
       and usbd_cdc_if.c files. See the project documentation for
       instructions on generating these with STM32CubeMX. */
}

/* ── IRQ Handlers ───────────────────────────────────────────────────────── */

void TIM6_DAC_IRQHandler(void)
{
    DDS_TIM6_IRQHandler();  /* Call DDS handler (defined in dds.c) */
}

void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

void USB_LP_IRQHandler(void)
{
    /* Handled by USB device library */
    extern USBD_HandleTypeDef hUsbDeviceFS;
    HAL_PCD_IRQHandler((PCD_HandleTypeDef *)hUsbDeviceFS.pData);
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while(1); }
void MemManage_Handler(void) { while(1); }
void BusFault_Handler(void) { while(1); }
void UsageFault_Handler(void) { while(1); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { HAL_IncTick(); }

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
        for (volatile uint32_t i = 0; i < 5000000; i++);
    }
}
