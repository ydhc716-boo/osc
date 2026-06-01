# STM32CubeMX 工程配置指南

## 操作步骤

### 1. 新建工程
- 打开 STM32CubeMX → 新工程
- 芯片选择: **STM32G474VET6** (LQFP100)

### 2. 引脚配置 (Pinout)

| 引脚 | 功能 | 备注 |
|------|------|------|
| PA0  | ADC1_IN1 | 示波器输入 |
| PA4  | DAC1_OUT1 | 信号发生器输出 |
| PA5  | GPIO_Output | 状态 LED |
| PA9  | GPIO_Input | USB VBUS 检测 |
| PA11 | USB_DM | USB OTG FS |
| PA12 | USB_DP | USB OTG FS |
| PC14 | OSC32_IN | 低速外部晶振（可选）|
| PC15 | OSC32_OUT | 低速外部晶振（可选）|
| PH0  | OSC_IN | 高速外部晶振 8MHz |
| PH1  | OSC_OUT | 高速外部晶振 8MHz |

### 3. 时钟配置 (Clock Configuration)
- HSE: 8 MHz 外部晶振
- PLL: ÷2 → ×85 → ÷2 → 170 MHz SYSCLK
- HCLK = SYSCLK = 170 MHz
- APB1 = APB2 = 170 MHz

### 4. 外设配置 (Peripherals)

**TIM6 (基本定时器)**
- Prescaler: 0, Counter Period: 169
- TRGO: Update Event → 触发 DAC
- NVIC: 使能 TIM6 全局中断

**TIM2 (通用定时器)**
- Prescaler: 0, Counter Period: 1699 (默认 100 kHz)
- Channel 1: PWM Generation, Pulse: 849
- TRGO: OC1 → 触发 ADC

**DAC1**
- Channel 1: Connected to external pin (PA4)
- Trigger: TIM6 TRGO

**ADC1**
- Channel 1: Single-ended (PA0)
- 12-bit resolution
- External Trigger: TIM2 TRGO, Rising Edge
- DMA Continuous Requests: Enabled
- DMA: Circular mode, Half Word

**USB_OTG_FS**
- Mode: Device Only
- VBUS Sensing: Disabled (use PA9 input if needed)

**USART2 (可选，调试)**
- Mode: Asynchronous, 115200-8-N-1
- PA2=TX, PA3=RX

### 5. 中间件配置 (Middleware)
- **USB_DEVICE**: Communication Device Class (CDC)
- 使能 CDC ACM

### 6. 项目设置 (Project Settings)
- 项目名称: `sco_firmware`
- Toolchain: Makefile / STM32CubeIDE / MDK-ARM (任选)
- 勾选 "Generate peripheral initialization as C code"
- 勾选 "Set all free pins as analog"

### 7. 生成代码

点击 **GENERATE CODE**，将生成以下关键文件:
```
Core/Src/main.c              → 替换为项目中的 main.c
Core/Src/stm32g4xx_it.c      → 中断服务程序
Core/Src/stm32g4xx_hal_msp.c → 外设 MSP 初始化
Core/Src/usb_device.c        → USB 设备初始化
Core/Src/usbd_cdc_if.c       → USB CDC 接口 → 替换为项目中的版本
Core/Inc/main.h              → 替换为项目中的 main.h
Core/Inc/stm32g4xx_hal_conf.h
USB_DEVICE/App/usb_device.c
```

### 8. 整合项目代码

CubeMX 生成代码后：
1. 将 `Core/Src/main.c` 替换为项目提供的版本（包含完整的主循环逻辑）
2. 将 `Core/Src/usbd_cdc_if.c` 替换为项目提供的版本（包含 CDC_Receive_Callback）
3. 添加 `Core/Src/dds.c`、`Core/Src/scope.c`、`Core/Src/protocol.c`
4. 添加 `Core/Inc/dds.h`、`Core/Inc/scope.h`、`Core/Inc/protocol.h`
5. 在 CubeMX 生成的 `main.h` 中添加 `SystemState` 结构和外设句柄声明

### 9. 构建

```bash
cd firmware
make clean
make
```

### 10. 烧录

```bash
make flash          # ST-Link (st-flash)
# 或
make flash-ocd      # ST-Link (openocd)
# 或在 STM32CubeIDE 中直接 Debug
```
