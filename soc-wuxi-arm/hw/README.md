# SoC 音频采集与 SPI Flash 接口说明

本文档面向软件同学，说明当前硬件已经提供的音频采集、ADPCM 压缩读取、SPI Flash 访问接口，以及软件侧建议的使用流程。

当前硬件目标是：

```text
INMP441 麦克风 -> I2S 接收 -> 可选 IMA ADPCM 压缩 -> CPU -> WAV 打包 -> SPI Flash(W25Q64)
```

## 总体地址映射

BusMatrix 当前主要地址区域如下：

| 地址范围 | 连接对象 | 说明 |
| --- | --- | --- |
| `0x0000_0000` 附近 | 片上 SRAM / 代码存储区域 | 原工程已有 |
| `0x4000_0000 ~ 0x7FFF_FFFF` | APB 子系统 `MI1` | UART、GPIO、Timer、SPI SSP 等外设 |
| `0x2001_0000 ~ 0x2001_FFFF` | 音频 AHB 外设 `MI2` | `audio_ahb_if` |

本次新增/重点使用三个基地址：

```c
#define AUDIO_BASE      0x20010000u
#define SPI0_BASE       0x4000C000u
#define SPI_FLASH_CS_BASE 0x4000D000u
```

`SPI0_BASE` 来自 APB 子系统的扩展端口 `ext12`。APB 子系统使用 `PADDR[15:12]` 选择外设，`ext12` 对应 nibble `0xC`，因此在 APB 基址 `0x4000_0000` 上偏移 `0xC000`。

`SPI_FLASH_CS_BASE` 来自 APB 子系统的扩展端口 `ext13`。`ext13` 对应 nibble `0xD`，因此在 APB 基址 `0x4000_0000` 上偏移 `0xD000`。该地址只用于软件手动控制 W25Q64 的 `CS#`。

## 音频硬件链路

音频链路有两种模式，由 `audio_ahb_if.CONTROL[3]` 选择。

```text
PCM 模式：
INMP441 -> i2s_rx_inmp441 -> PCM FIFO -> audio_ahb_if -> CPU

压缩模式：
INMP441 -> i2s_rx_inmp441 -> PCM FIFO -> ima_adpcm_encoder -> ADPCM FIFO -> audio_ahb_if -> CPU
```

I2S 接收模块已经完成 INMP441 左声道采样和位宽处理，写入 FIFO 的数据已经是 `16-bit PCM`。软件不需要再做 `24-bit -> 16-bit` 转换。

当前 FIFO 配置：

| FIFO | 数据宽度 | 深度 | 说明 |
| --- | --- | --- | --- |
| PCM FIFO | 16 bit | 1024 word | I2S 接收模块写入 |
| ADPCM FIFO | 32 bit | 1024 word | 每个 word 包含 8 个 4-bit ADPCM code |

## 音频寄存器

```c
#define AUDIO_CONTROL   (*(volatile unsigned int *)(AUDIO_BASE + 0x00u))
#define AUDIO_STATUS    (*(volatile unsigned int *)(AUDIO_BASE + 0x20u))
#define AUDIO_DATA      (*(volatile unsigned int *)(AUDIO_BASE + 0x40u))
```

### `CONTROL`，偏移 `0x00`

| 位 | 名称 | 说明 |
| --- | --- | --- |
| 0 | `rx_enable` | 写 `1` 开始 I2S 接收，写 `0` 停止接收 |
| 1 | `i2s_clear` | 写 `1` 产生一个 HCLK 周期的 I2S 接收状态清零脉冲 |
| 2 | `fifo_clear` | 写 `1` 产生一个 HCLK 周期的 FIFO 清空脉冲，会同时清空 PCM FIFO 和 ADPCM FIFO |
| 3 | `compress_enable` | `0` 为 PCM 模式，`1` 为 ADPCM 压缩模式 |

注意：`i2s_clear` 和 `fifo_clear` 是脉冲控制位，不会保持为 1。软件通常先写清零位，再写正式运行配置。

### `STATUS`，偏移 `0x20`

| 位 | 名称 | 说明 |
| --- | --- | --- |
| 0 | `fifo_empty` | 当前模式对应的输出 FIFO 为空 |
| 1 | `fifo_full` | 当前模式对应的输出 FIFO 已满 |
| 2 | `sample_valid` | I2S 收到一个新 PCM 样本的单周期状态 |
| 3 | `rx_enable` | 当前 I2S 接收使能状态 |
| `[14:4]` | `fifo_usedw` | 当前模式对应的输出 FIFO 已用深度 |
| 15 | `compress_enable` | 当前是否处于 ADPCM 压缩模式 |

`fifo_usedw` 的单位取决于模式：

```text
PCM 模式：单位是 16-bit PCM sample
ADPCM 模式：单位是 32-bit ADPCM packed word
```

### `DATA`，偏移 `0x40`

读取 `DATA` 会从当前模式对应的 FIFO 弹出一个数据。

| 模式 | `DATA` 返回值 |
| --- | --- |
| `compress_enable = 0` | `HRDATA[15:0]` 为一个 `16-bit PCM` 样本，`HRDATA[31:16]` 为 0 |
| `compress_enable = 1` | `HRDATA[31:0]` 为一个 packed ADPCM word |

ADPCM 打包顺序为低 nibble 优先：

```text
word[3:0]    = code0
word[7:4]    = code1
...
word[31:28]  = code7
```

## 音频软件轮询示例

### PCM 模式读取

```c
#define AUDIO_CTRL_RX_ENABLE   (1u << 0)
#define AUDIO_CTRL_I2S_CLEAR   (1u << 1)
#define AUDIO_CTRL_FIFO_CLEAR  (1u << 2)
#define AUDIO_CTRL_COMPRESS    (1u << 3)

#define AUDIO_STATUS_EMPTY     (1u << 0)

static void audio_start_pcm(void)
{
    AUDIO_CONTROL = AUDIO_CTRL_I2S_CLEAR | AUDIO_CTRL_FIFO_CLEAR;
    AUDIO_CONTROL = AUDIO_CTRL_RX_ENABLE;
}

static unsigned short audio_read_pcm_polling(void)
{
    while (AUDIO_STATUS & AUDIO_STATUS_EMPTY) {
        /* wait */
    }

    return (unsigned short)(AUDIO_DATA & 0xFFFFu);
}
```

### ADPCM 压缩模式读取

```c
static void audio_start_adpcm(void)
{
    AUDIO_CONTROL = AUDIO_CTRL_I2S_CLEAR | AUDIO_CTRL_FIFO_CLEAR | AUDIO_CTRL_COMPRESS;
    AUDIO_CONTROL = AUDIO_CTRL_RX_ENABLE | AUDIO_CTRL_COMPRESS;
}

static unsigned int audio_read_adpcm_word_polling(void)
{
    while (AUDIO_STATUS & AUDIO_STATUS_EMPTY) {
        /* wait */
    }

    return AUDIO_DATA;
}
```

如果后续要录制 10 到 30 秒，建议软件持续轮询读取，不能等 FIFO 很满才读。当前 FIFO 主要用于吸收短时间抖动，不适合作为长时间缓存。

### 已知硬件问题：HADDR[4:2] 地址别名（2026-05-04 已修复）

上板测试发现 **STATUS**（偏移 `0x04`）和 **DATA**（偏移 `0x08`）读回的值都等于 **CONTROL**（偏移 `0x00`），且 `fifo_empty`、`rx_enable` 等字段均不反映真实硬件状态。

**根因分析：**

`audio_ahb_if.v` 使用 `word_addr = HADDR[7:2]` 进行寄存器地址解码。原映射使用 HADDR[4:2] 的低三位区分寄存器：

| 寄存器 | 原 word_addr | 原 C 偏移 | 依赖位 |
|---|---|---|---|
| CONTROL | `6'h00` | `+0x00` | HADDR[4:2]=000 |
| STATUS | `6'h01` | `+0x04` | HADDR[2]=1 |
| DATA | `6'h02` | `+0x08` | HADDR[3]=1 |

v2 上板测试发现 PCM 数据全部等于 CONTROL[15:0]（`0x0001`），ADPCM 数据全部等于 CONTROL[31:0]（`0x00000009`），证实 **HADDR[4:2] 三位全部 stuck-at-0**。

**修复方案（已应用）：**

改用 HADDR[7:5]（`word_addr[5:3]`）区分寄存器，避免使用已确认 stuck 的低三位：

| 寄存器 | 新 word_addr | 新 C 偏移 | 依赖位 |
|---|---|---|---|
| CONTROL | `6'h00` | `+0x00` | 不变 |
| STATUS | `6'h08` | `+0x20` | HADDR[5]=1 |
| DATA | `6'h10` | `+0x40` | HADDR[6]=1 |

对应的 C 代码更新：
```c
#define AUDIO_CONTROL   (*(volatile uint32_t *)0x20010000UL)
#define AUDIO_STATUS    (*(volatile uint32_t *)0x20010020UL)  // was 0x04
#define AUDIO_DATA      (*(volatile uint32_t *)0x20010040UL)  // was 0x08
```

**验证方法：**

烧录含 remap 的 bitstream 后，运行 `main.c` v4。预期：
- STATUS 不再等于 CONTROL
- `fifo_empty` 反映真实 FIFO 状态
- PCM/ADPCM 数据读取正常

如果 STATUS 仍等于 CONTROL 或 DATA 仍读不到正确数据，说明 HADDR[7:5] 中也有 stuck bits，需用 SignalTap 抓取 `haddrmi2[7:2]` 完整波形确认。

2026-05-04 的 v4 上板结果显示，改到 `+0x20/+0x40` 后 STATUS/DATA 仍然返回 CONTROL：

```text
STAT after RX_ENABLE=00000001
PCM DATA reads: 0x0001
ADPCM DATA reads: 0x00000009
```

`main.c` 的地址扫描进一步显示 `+0x00` 会读到旧/随机值，而后续偏移多读回 CONTROL。这更像 AHB-Lite 读数据阶段错拍，而不是单纯地址线 stuck：读地址在 ADDRESS phase 有效，`HRDATA` 需要在下一拍 DATA phase 稳定返回。`audio_ahb_if.v` 已改为在读地址阶段寄存要返回的 `HRDATA`，DATA 读同时弹出 FIFO。

下一步重新综合并烧录包含读通道修复的 bitstream，再运行 `main.c` 地址扫描。预期 `+0x20` 不再等于 CONTROL，`+0x40` 可以读出 FIFO 数据。如果仍全部偏移都读回 CONTROL，再优先检查 bitstream 是否更新或用 SignalTap 抓 `haddrmi2[7:2]`。

**如果 HADDR[7:5] 也有问题：**

可以进一步扩展到使用 HADDR[11:8] 等更高位，或彻底排查 BusMatrix MI2 端口的 HADDR 输出。

## SPI Flash 硬件连接

当前顶层已经接入 ARM PL022 SSP 控制器，挂在 APB `ext12`。另外，W25Q64 的 `CS#` 不再直接使用 PL022 的 `SSPFSSOUT`，而是由 APB `ext13` 上的 1-bit 手动片选寄存器控制。

硬件链路：

```text
CPU -> APB ext12 -> PL022 SSP -> W25Q64 SCK/MOSI/MISO
CPU -> APB ext13 -> manual CS register -> W25Q64 CS#
```

顶层信号：

| 顶层信号 | 方向 | 连接 W25Q64 |
| --- | --- | --- |
| `spi_flash_miso` | input | `DO / IO1` |
| `spi_flash_mosi` | output | `DI / IO0` |
| `spi_flash_sck` | output | `CLK` |
| `spi_flash_cs_n` | output | `CS#` |

当前建议管脚分配：

| 顶层信号 | FPGA 管脚 | DE10-Lite GPIO |
| --- | --- | --- |
| `spi_flash_miso` | `PIN_W5` | `GPIO_10` |
| `spi_flash_mosi` | `PIN_AA15` | `GPIO_11` |
| `spi_flash_sck` | `PIN_AA14` | `GPIO_12` |
| `spi_flash_cs_n` | `PIN_W13` | `GPIO_13` |

W25Q64 供电和固定管脚：

```text
VCC  -> 3.3V
GND  -> GND
WP#  -> 3.3V
HOLD# / RESET# -> 3.3V
```

不要把 W25Q64 接到 5V。

## SPI Flash 手动片选寄存器

W25Q64 的一条命令通常包含多个 byte，例如 `0x9F + 3 个 dummy byte`，或者 `0x03 + 24-bit address + data`。这些 byte 必须在同一次 `CS#` 低电平期间完成。因此当前硬件使用软件手动控制 `spi_flash_cs_n`。

```c
#define SPI_FLASH_CS_BASE  (*(volatile unsigned int *)0x4000D000u)
```

寄存器定义：

| 地址 | 位 | 名称 | 说明 |
| --- | --- | --- | --- |
| `0x4000_D000` | 0 | `spi_flash_cs_n` | `1` 表示 `CS#` 高，Flash 未选中；`0` 表示 `CS#` 低，Flash 选中 |

复位后该位默认为 `1`，即 Flash 未选中。

建议软件封装：

```c
static void flash_cs_low(void)
{
    SPI_FLASH_CS_BASE = 0u;
}

static void flash_cs_high(void)
{
    SPI_FLASH_CS_BASE = 1u;
}
```

读 JEDEC ID 的片选时序应类似：

```c
static void w25_read_jedec_id(unsigned char id[3])
{
    spi0_init_mode0_1mhz();

    flash_cs_low();
    spi0_xfer8(0x9Fu);
    id[0] = spi0_xfer8(0xFFu);
    id[1] = spi0_xfer8(0xFFu);
    id[2] = spi0_xfer8(0xFFu);
    flash_cs_high();
}
```

## PL022 SSP 寄存器

PL022 SSP 寄存器基地址：

```c
#define SPI0_BASE       0x4000C000u
```

常用寄存器：

```c
#define SPI_CR0         (*(volatile unsigned int *)(SPI0_BASE + 0x00u))
#define SPI_CR1         (*(volatile unsigned int *)(SPI0_BASE + 0x04u))
#define SPI_DR          (*(volatile unsigned int *)(SPI0_BASE + 0x08u))
#define SPI_SR          (*(volatile unsigned int *)(SPI0_BASE + 0x0Cu))
#define SPI_CPSR        (*(volatile unsigned int *)(SPI0_BASE + 0x10u))
#define SPI_IMSC        (*(volatile unsigned int *)(SPI0_BASE + 0x14u))
#define SPI_ICR         (*(volatile unsigned int *)(SPI0_BASE + 0x20u))
```

`CR0` 常用位：

| 位 | 名称 | 说明 |
| --- | --- | --- |
| `[3:0]` | `DSS` | 数据宽度，8-bit 传输写 `7` |
| `[5:4]` | `FRF` | 帧格式，SPI/Motorola 格式写 `0` |
| 6 | `SPO` | SPI clock polarity，W25Q64 mode 0 时写 `0` |
| 7 | `SPH` | SPI clock phase，W25Q64 mode 0 时写 `0` |
| `[15:8]` | `SCR` | 串行时钟分频参数 |

`CR1` 常用位：

| 位 | 名称 | 说明 |
| --- | --- | --- |
| 0 | `LBM` | Loopback mode，正常写 `0` |
| 1 | `SSE` | SSP enable，写 `1` 使能 |
| 2 | `MS` | Master/slave select，主机模式写 `0` |
| 3 | `SOD` | Slave output disable，主机模式通常写 `0` |

`SR` 常用位：

| 位 | 名称 | 说明 |
| --- | --- | --- |
| 0 | `TFE` | TX FIFO empty |
| 1 | `TNF` | TX FIFO not full |
| 2 | `RNE` | RX FIFO not empty |
| 3 | `RFF` | RX FIFO full |
| 4 | `BSY` | SSP busy |

SSP 输出时钟近似为：

```text
SSPCLKOUT = SSPCLK / (CPSDVSR * (1 + SCR))
```

当前 `SSPCLK = 50 MHz`。建议先用低速验证，例如：

```text
CPSR = 10
SCR  = 4
SPI clock = 50 MHz / (10 * (1 + 4)) = 1 MHz
```

初始化示例：

```c
#define SPI_SR_TFE      (1u << 0)
#define SPI_SR_TNF      (1u << 1)
#define SPI_SR_RNE      (1u << 2)
#define SPI_SR_BSY      (1u << 4)

static void spi0_init_mode0_1mhz(void)
{
    SPI_CR1 = 0x0000u;        /* disable SSP */
    SPI_CPSR = 10u;           /* even divider */
    SPI_CR0 = (7u << 0)       /* DSS = 8-bit */
            | (0u << 4)       /* FRF = SPI */
            | (0u << 6)       /* SPO = 0 */
            | (0u << 7)       /* SPH = 0 */
            | (4u << 8);      /* SCR = 4 */
    SPI_IMSC = 0x0000u;       /* polling mode, disable interrupts */
    SPI_ICR = 0x0003u;        /* clear sticky interrupt flags if present */
    SPI_CR1 = (1u << 1);      /* SSE = 1, master mode */
}

static unsigned char spi0_xfer8(unsigned char tx)
{
    while ((SPI_SR & SPI_SR_TNF) == 0u) {
        /* wait TX FIFO not full */
    }

    SPI_DR = tx;

    while ((SPI_SR & SPI_SR_RNE) == 0u) {
        /* wait RX FIFO not empty */
    }

    return (unsigned char)(SPI_DR & 0xFFu);
}

static void spi0_wait_idle(void)
{
    while (SPI_SR & SPI_SR_BSY) {
        /* wait */
    }
}
```

## W25Q64 常用命令

请以 `C:\work\seu_hw\soc_design\W25Q64.pdf` 为最终依据。常用命令如下：

| 命令 | 值 | 说明 |
| --- | --- | --- |
| `Write Enable` | `0x06` | 写/擦除前必须先发 |
| `Read Status Register-1` | `0x05` | 读取 BUSY/WEL 等状态 |
| `Read Data` | `0x03` | 普通读数据 |
| `Page Program` | `0x02` | 页编程，通常最多 256 byte，不能跨页 |
| `Sector Erase` | `0x20` | 4KB 扇区擦除 |
| `JEDEC ID` | `0x9F` | 读厂商/型号 ID |

状态寄存器常用位：

```c
#define W25_SR_BUSY     (1u << 0)
#define W25_SR_WEL      (1u << 1)
```

最小验证建议先读 JEDEC ID：

```c
static void w25_read_jedec_id(unsigned char id[3])
{
    spi0_init_mode0_1mhz();

    flash_cs_low();
    spi0_xfer8(0x9Fu);
    id[0] = spi0_xfer8(0xFFu);
    id[1] = spi0_xfer8(0xFFu);
    id[2] = spi0_xfer8(0xFFu);
    flash_cs_high();

    spi0_wait_idle();
}
```

W25Q64 常见 JEDEC ID 是：

```text
EF 40 17
```

不同厂商兼容芯片可能不完全一样，所以以手册和实物为准。

## Flash 写入流程建议

写入 Flash 之前必须擦除。建议软件按 4KB sector 管理存储空间。

典型流程：

```text
1. 读 JEDEC ID，确认 SPI 通信正常。
2. 选择一个 Flash 起始地址，例如 0x000000。
3. 对要写入的区域执行 4KB Sector Erase。
4. 每次擦除或写入前先发 Write Enable(0x06)。
5. 发 Sector Erase(0x20 + 24-bit address)，轮询 BUSY=0。
6. 按 256B page 写数据，Page Program(0x02 + 24-bit address + data)。
7. 每写一页后轮询 BUSY=0。
8. 写完后可用 Read Data(0x03) 回读校验。
```

页写注意事项：

- `Page Program` 一次最多 256 byte。
- 不要让一次 Page Program 跨越 256B page 边界。
- 写入只能把 bit 从 `1` 编程为 `0`，重新写之前必须擦除对应 sector。

## WAV 打包建议

如果保存原始 PCM WAV：

- WAV 头通常 44 byte。
- `AudioFormat = 1` 表示 PCM。
- `BitsPerSample = 16`。
- `NumChannels = 1`。
- `SampleRate` 应和当前 I2S WS 采样率一致，当前约为 `15.625 kHz`。

如果保存 IMA ADPCM WAV：

- WAV 格式不再是普通 PCM，需要使用 IMA ADPCM 的 `fmt` 扩展格式。
- 当前硬件输出的是连续 ADPCM nibble packed word，但还没有生成 WAV block header。
- 软件若要生成标准 IMA ADPCM WAV，需要按 WAV IMA ADPCM block 格式组织数据，而不是简单把 packed word 直接写成 PCM WAV。

为了课程阶段更稳，建议优先完成：

```text
ADPCM 裸数据写入 Flash -> 回读校验 -> 再考虑标准 WAV/IMA WAV 文件头
```

如果老师要求电脑直接播放 `.wav`，再补标准 WAV block 封装。

## 当前硬件实现文件

主要文件：

- `arm-soc/top.v`：顶层，接入 I2S、ADPCM、audio AHB、PL022 SSP。
- `hw/i2s_rx_inmp441.v`：I2S 接收。
- `hw/fifo_sync.v`：同步 FIFO。
- `hw/audio_ahb_if.v`：音频 AHB 寄存器接口。
- `hw/ima_adpcm_encoder.v`：IMA ADPCM 编码和 32-bit 打包。
- `ARM_Cortex_M3_DesignStart_Eval/.../smm/logical/pl022_ssp/verilog/*.v`：ARM PL022 SSP 控制器源码。

当前顶层新增 SPI Flash 端口：

```verilog
input  wire spi_flash_miso;
output wire spi_flash_mosi;
output wire spi_flash_sck;
output wire spi_flash_cs_n;
```

## 仿真说明

当前提供四个 ModelSim testbench：

- `tb_i2s_rx_inmp441.v`：验证单独 I2S 接收模块。
- `tb_audio_ahb.v`：验证 `I2S + PCM FIFO + AHB`。
- `tb_audio_ahb_adpcm.v`：验证 `I2S + PCM FIFO + ADPCM + ADPCM FIFO + AHB`。
- `tb_ima_adpcm_encoder.v`：验证 IMA ADPCM 基本组包行为。

推荐命令：

```tcl
cd C:/work/seu_hw/soc_design/soc-wuxi-arm/hw
vlib audio_tb_run1
vlog -work audio_tb_run1 fifo_sync.v i2s_rx_inmp441.v audio_ahb_if.v ima_adpcm_encoder.v tb_audio_ahb.v tb_audio_ahb_adpcm.v
vsim audio_tb_run1.tb_audio_ahb
add wave -r *
run -all
```

压缩通路：

```tcl
vsim audio_tb_run1.tb_audio_ahb_adpcm
add wave -r *
run -all
```

如果 ModelSim 提示 `_lock` 文件被占用，换一个新的库名，例如 `audio_tb_run2`。

## 编译与时序状态

当前 Quartus Full Compilation 已经成功：

```text
Full Compilation was successful. 0 errors
```

但 TimeQuest 报告中仍有负 slack：

```text
Worst-case setup slack    < 0
Worst-case recovery slack < 0
Worst-case min pulse width slack < 0
```

这表示 bitstream 可以生成，但严格意义上时序还没有完全收敛。课程实验阶段可以先继续做功能验证；如果后续上板行为不稳定，需要回来处理时序约束、复位约束、TCK 调试时钟约束，以及 ARM 参考工程自带逻辑的 timing warning。

### 当前负 slack 的来源判断

根据 TimeQuest 的最坏路径报告，目前最严重的 setup 违例路径位于 ARM Cortex-M3 参考核心内部，路径起点和终点都显示在：

```text
CORTEXM3INTEGRATIONDS -> cortexm3ds_logic
```

这说明当前最坏时序路径不是下面这些我们新增的录音链路模块：

- `i2s_rx_inmp441`
- `fifo_sync`
- `audio_ahb_if`
- `ima_adpcm_encoder`
- `PL022 SSP / SPI Flash` 接口

也就是说，当前负 slack 主要反映的是老师提供的 Cortex-M3 参考核心在 `50 MHz` 系统时钟下的内部时序压力。新增的音频/压缩/SPI 模块会增加资源和布线压力，可能对整体布局布线有间接影响，但从最坏路径归属看，当前主要瓶颈不在录音系统本身。

前面曾经出现过一条 `PCM FIFO -> ADPCM predictor` 的音频路径时序压力，已经通过在 `ima_adpcm_encoder` 内部增加输入寄存器解决。现在 TimeQuest 最坏路径已经切换到 M3 内部，这可以作为音频链路当前不是主要时序瓶颈的依据。

因此现阶段建议：

- 不要为了消除该负 slack 直接降低 `HCLK`，因为当前工程中 CPU、AHB、APB、SPI 和 I2S 分频逻辑都直接使用顶层 `CLK`。
- 如果降低系统时钟，`i2s_rx_inmp441` 生成的 `mic_sck` 和 `mic_ws` 也会一起变化，从而改变麦克风采样率。
- 课程阶段可以先按 `50 MHz` 继续做功能验证；如果上板后 CPU 或总线行为不稳定，再考虑使用老师提供的参考约束、降低系统时钟、增加 PLL 分频，或对 Cortex-M3 参考工程进行专门时序优化。

## SPI Flash CS# 注意事项

当前 `spi_flash_cs_n` 已经改为软件手动控制，不再直接接 PL022 的 `SSPFSSOUT`。SPI Flash 的多字节命令通常要求 `CS#` 在整个命令、地址、数据阶段保持低电平。

上板后请优先用逻辑分析仪或 SignalTap 观察：

```text
spi_flash_cs_n
spi_flash_sck
spi_flash_mosi
spi_flash_miso
```

如果发现 `CS#` 在每个 byte 之间都被拉高，说明软件没有正确使用 `0x4000_D000` 的手动片选寄存器，或者仍在观察 PL022 内部的 `SSPFSSOUT` 而不是顶层 `spi_flash_cs_n`。正确做法是在一次完整 Flash 命令开始前写 `0x4000_D000 = 0`，命令结束后再写 `0x4000_D000 = 1`。
