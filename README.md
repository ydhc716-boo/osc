## SCO — 简易信号源与示波器

## 基于 STM32G474VET6 的 USB 虚拟示波器与信号发生器系统。仅需一根 USB 数据线连接电脑，即可在浏览器中生成波形信号并查看实时波形。

 功能规格

| 项目 | 参数 |
|------|------|
| 信号源波形 | 正弦波、方波、三角波、锯齿波 |
| 信号源频率 | 1 Hz — 100 kHz |
| 信号源幅度 | 0 — 3.3 Vpp |
| 示波器采样率 | 1 kSPS — 1 MSPS |
| 示波器触发 | 自动 / 上升沿 / 下降沿 |
| 通信接口 | USB CDC 虚拟串口 |
| 上位机界面 | Web 网页，浏览器访问 |
| 模拟器 | Python TCP Server，无需硬件开发调试 |

**系统架构：**

```
                        PC 上位机
   ┌───────────────────────────────────────────────┐
   │  浏览器 (Canvas 波形 + 控制面板)                │
   │       ↕ WebSocket + REST                      │
   │  Flask 后端 (串口管理 / 协议编解码 / 推送)      │
   └─┬─────────────────────────┬───────────────────┘
     ↕ USB CDC (COM)           ↕ TCP :9876
   ┌──────────────┐    ┌──────────────────┐
   │ STM32G474 HW │    │  MCU 模拟器 (Python) │
   │ DDS+DAC+ADC  │    │  虚拟 DDS/ADC/环回  │
   └──────────────┘    └──────────────────┘
```

## 项目结构

```
SCO/
├── README.md
├── requirements.txt
├── firmware/                    # 单片机固件 (C + HAL)
│   ├── Core/Inc/                # main.h, dds.h, scope.h, protocol.h
│   ├── Core/Src/                # main.c, dds.c, scope.c, protocol.c, ...
│   ├── CUBEMX_SETUP.md          # CubeMX 配置指南
│   ├── Makefile
│   └── STM32G474VETx_FLASH.ld
├── simulator/                   # MCU 模拟器 (Python TCP Server)
│   ├── main.py                  # TCP 服务入口
│   ├── dds_sim.py               # DDS 数学模拟
│   ├── adc_sim.py               # ADC 虚拟采样
│   └── protocol.py              # 协议编解码
└── web_ui/                      # PC 上位机 (Flask + Canvas)
    ├── server.py                # Flask 后端 + SocketIO
    ├── serial_handler.py        # 串口/TCP 连接管理
    ├── protocol.py
    ├── templates/index.html
    └── static/
        ├── css/style.css
        └── js/                  # app.js, websocket.js, waveform.js, controls.js
```

## 环境要求

| 组件 | 要求 |
|------|------|
| Python | 3.10+ |
| 浏览器 | Chrome / Edge / Firefox |
| 硬件 (可选) | STM32G474VET6 开发板 |
| 编译工具 (可选) | arm-none-eabi-gcc + ST-Link |

## 快速开始

**1. 安装依赖**

```bash
cd SCO
pip install -r requirements.txt
```

**2. 启动模拟器 (无需硬件)**

```bash
python -m simulator.main --loopback
```

模拟器监听 `127.0.0.1:9876`，环回模式将 DDS 输出接入 ADC 输入。

**3. 启动 Web 上位机**

```bash
python web_ui/server.py
```

Web 服务监听 `http://127.0.0.1:5000`。

**4. 打开浏览器**

访问 `http://127.0.0.1:5000`，点击 "连接设备" 选择 TCP 模式连接，配置信号源参数后启动信号源和采集即可查看实时波形。

**5. 使用真实硬件 (可选)**

参考 `firmware/CUBEMX_SETUP.md` 用 CubeMX 生成 HAL 驱动，`cd firmware && make && make flash` 烧录后 USB 连接开发板，Web 界面选择 "串口" 模式连接。

## 通信协议

数据包格式（二进制）：

```
┌──────┬──────┬──────┬───────┬───────┬─────────┬────────┐
│ SYNC │ SYNC │ CMD  │ LEN_H │ LEN_L │  DATA   │ CRC16  │
│ 0xAA │ 0x55 │  1B  │  1B   │  1B   │ N Bytes │ 2B LE  │
└──────┴──────┴──────┴───────┴───────┴─────────┴────────┘
```

CRC16-CCITT 覆盖 CMD + LEN + DATA。

**PC → MCU 命令**

| 命令 | 代码 | 数据 |
|------|------|------|
| SET_WAVEFORM | 0x01 | `[type:1B]` 0=正弦 1=方波 2=三角波 3=锯齿波 |
| SET_FREQ | 0x02 | `[freq_Hz:4B LE]` |
| SET_AMPLITUDE | 0x03 | `[mV:2B LE]` |
| SET_SAMPLE_RATE | 0x04 | `[rate_Hz:4B LE]` |
| SET_TRIGGER | 0x05 | `[mode:1B] [level_mV:2B LE]` |
| START_SG | 0x06 | -- |
| STOP_SG | 0x07 | -- |
| START_SCOPE | 0x08 | `[samples_per_pkt:2B LE]` |
| STOP_SCOPE | 0x09 | -- |
| GET_STATUS | 0x0A | -- |

**MCU → PC 响应**

| 响应 | 代码 | 数据 |
|------|------|------|
| STATUS | 0x80 | `[sg_on:1B] [scope_on:1B] [waveform:1B] [freq:4B] [amp:2B] [trig_mode:1B] [trig_level:2B] [sample_rate:4B]` |
| SCOPE_DATA | 0x81 | `[seq:2B] [count:2B] [samples:count*2B LE]` |
| ERROR | 0x82 | `[code:1B]` |

## 固件说明

**硬件资源**

| 外设 | 引脚 | 用途 | 配置 |
|------|------|------|------|
| DAC1_CH1 | PA4 | 信号源输出 | 12bit, TIM6 DMA @ 1MHz |
| ADC1_CH1 | PA0 | 示波器输入 | 12bit, TIM2 DMA 双缓冲 |
| TIM6 | -- | DAC 触发 | 1MHz |
| TIM2 | -- | ADC 触发 | 可配置 1k–1M SPS |
| USB OTG FS | PA11/PA12 | PC 通信 | CDC ACM |
| GPIO | PA5 | 状态 LED | 心跳指示 |

**DDS 信号发生器** — 32 位相位累加器 + 256 点正弦查找表。频率分辨率约 0.00023 Hz，DAC 更新率 1 MSPS。方波、三角波、锯齿波实时计算，幅度通过中点缩放。

**示波器** — DMA 双缓冲 (1024 点，A/B 半满交替)。半满中断触发数据打包与 USB 发送。触发模式支持自动、上升沿、下降沿，触发点对齐至缓冲区起始。

## Web 界面

```
┌──────────────┬──────────────────────┐
│  信号源控制   │                      │
│  波形/频率/幅 │    波形显示 (Canvas)  │
│  [启动/停止]  │    网格 + 触发电平线  │
├──────────────┤                      │
│  示波器控制   │                      │
│  采样率/时基  ├──────────────────────┤
│  触发/电平   │  测量数据             │
│  [开始/停止]  │  Vpp / 频率 / 平均值 │
├──────────────┤  最小值 / 最大值     │
│  连接状态     │  丢包率              │
└──────────────┴──────────────────────┘
```

**键盘快捷键**

| 快捷键 | 功能 |
|--------|------|
| Ctrl+S | 启动信号源 |
| Ctrl+T | 停止信号源 |
| Ctrl+O | 开始采集 |
| Ctrl+P | 停止采集 |

## 测试结果

测试环境：Windows 11 / Python 3.12 / Chrome 125 / 模拟器环回模式

| 测试项 | 结果 |
|--------|------|
| 模拟器启动 | 通过 |
| Web 界面启动 | 通过 |
| TCP 连接/断开 | 通过 |
| 正弦/方波/三角/锯齿波输出 | 通过 |
| 频率调节 1Hz–100kHz | 通过 |
| 幅度调节 0–3.3Vpp | 通过 |
| 采样率切换 1kSPS–1MSPS | 通过 |
| 触发模式 (自动/上升/下降) | 通过 |
| WebSocket 实时推送 | 通过 (延迟 < 50ms) |
| 测量功能 (Vpp/频率/平均值) | 通过 |

性能指标：波形刷新 60 FPS，数据传输延迟 < 30ms，模拟器 CPU 占用 < 5%。

## 运行截图

> 运行 `python -m simulator.main --loopback` 和 `python web_ui/server.py` 后，浏览器访问 `http://127.0.0.1:5000`。

1. **模拟器终端** — TCP 监听 127.0.0.1:9876，显示连接与命令日志
2. **Web 主界面** — 左侧控制面板 + 右侧波形显示区 + 底部测量数据
3. **连接对话框** — 切换 TCP/串口模式，输入地址和端口
4. **信号源控制** — 波形选择按钮、频率/幅度滑块与数字输入
5. **波形显示** — Canvas 实时渲染，网格标尺，触发电平线
6. **测量面板** — Vpp、频率、平均值、最大值、最小值、丢包率
7. **波形对比** — 正弦波、方波、三角波、锯齿波并排显示

*(请在此处添加实际运行截图)*

## 调试与开发

直接测试协议（无需 Web UI）：

```python
import socket, struct, sys
sys.path.insert(0, '.')
from simulator.protocol import *

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 9876))
sock.send(pack_set_freq(5000))
sock.send(pack_start_sg())
sock.send(pack_start_scope(512))

parser = ProtocolParser()
while True:
    data = sock.recv(4096)
    for cmd, d in parser.feed(data):
        if cmd == RSP_SCOPE_DATA:
            seq, count = struct.unpack('<HH', d[:4])
            print(f"Got {count} samples, seq={seq}")
```

列出可用串口：

```python
from web_ui.serial_handler import list_serial_ports
print(list_serial_ports())
```

## 故障排除

| 问题 | 解决 |
|------|------|
| 模拟器端口被占用 | `python -m simulator.main --port 9877` |
| Web 界面无法访问 | `python web_ui/server.py --port 8080` |
| 连接后无波形 | 确认已启动信号源和示波器采集 |
| 串口检测不到 | 检查 USB 驱动，确认设备管理器中有 COM 口 |
| 依赖安装报错 | `pip install -r requirements.txt` |
| 波形卡顿 | 降低采样率至 100kSPS 以下 |

## 后续计划

FFT 频谱分析 · 波形导出 (CSV/WAV) · X-Y 模式 (李萨如图形) · 多通道支持 · 扫频功能 · 数据压缩

## 许可证

MIT License

## 仓库地址

```
https://github.com/ydhc716-boo/osc
```
