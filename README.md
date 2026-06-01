## SCO — 简易信号源与示波器

基于 STM32G474VET6 的 USB 虚拟示波器与信号发生器系统。仅需一根 USB 数据线连接电脑，即可在浏览器中生成波形信号并查看实时波形。

## 功能规格

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




## 仓库地址

```
https://github.com/ydhc716-boo/osc
```
