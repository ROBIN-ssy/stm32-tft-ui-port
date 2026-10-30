# KK_OLED / KK_UI 接入说明

本工程已在原有 STM32F4 标准外设库（SPL）基础上接入 **KK_OLED 0.1.0** 与 **KK_UI 0.1.0**，
并安装 KK_UI 标准 Demo。本文档记录接入的事实依据、做法、已验证与未验证的部分。

---

## 1. 当前状态速览

| 项目 | 结果 |
|---|---|
| 编译 | **0 Error / 0 Warning**（UV4 批处理，ARMCC 5.06 update 6） |
| 资源占用 | Code 39706 B / RO-data 7290 B / RW-data 256 B / ZI-data 25408 B |
| 逻辑画布 | **320 × 240 横屏**（物理面板 240 × 320） |
| KK_OLED `graphics/` | **未修改**（仅 `driver/` 与物理尺寸宏按移植需要改写） |
| KK_UI 公共 API | **未修改**（仅迁移布局常量与各页 Draw 的绘制坐标） |
| 传输方式 | 阻塞刷新 + SPI2 TX DMA 异步刷新；IT 入口按契约返回 `OLED_UNSUPPORTED` |
| 实机烧录/目视验收 | **未执行**（见第 7 节） |

---

## 2. 已确认硬件事实及来源

| 事实 | 取值 | 来源 |
|---|---|---|
| MCU / SDK | STM32F407VETx，ST 标准外设库，Keil MDK 5.25 | `USER/LCD.uvprojx` |
| 面板 | 240×320、16 bpp RGB565、SPI | `HARDWARE/LCD/lcd_init.c` 初始化序列（ILI9341 风格命令表：0xCF/0xED/0xE8/0xCB/0xF7/0xEA） |
| SPI 引脚 | SCK=PB13、MISO=PB14、MOSI=PB15 → **SPI2** | `HARDWARE/LCD/lcd_init.h` |
| CS | PB12，**低电平选中 LCD**；该脚同时接板载字库芯片，高电平选字库 | `HARDWARE/LCD/lcd_init.h` 的 `LCD_CS_*` / `ZK_CS_*` 宏 |
| DC / 背光 | DC=PC5，BLK=PB1 | `HARDWARE/LCD/lcd_init.h` |
| 列/行偏移 | 0 | 原工程 `LCD_Address_Set()` 直接把逻辑坐标写入 0x2A/0x2B，无偏移 |
| 像素写入顺序 | 0x2A(x) 先递增，随后 0x2B(y) | `HARDWARE/LCD/lcd.c` 的 `LCD_Fill()` 循环顺序（外层 y、内层 x） |
| 面板方向 | MADCTL 0x78（横屏）。0xA8 与其相差 180° | 原工程 `USE_HORIZONTAL==2` 用的就是 0x78 + 320×240，属已验证取值 |
| 按键 | UP=PB7、DOWN=PB8、OK=PB9，按下接 GND、低有效、MCU 内部上拉 | 用户指定 |
| 逻辑画布 | 320×240 横屏 | 用户选择 |
| 刷新方式 | 阻塞 + SPI2 TX DMA | 用户选择 |

---

## 3. 关键取舍与原因

### 3.1 为什么逻辑画布用「旋转 90」而不是直接改物理尺寸

KK_OLED 的 `OLED_InternalGetTransferMinX/MaxX()` 返回类型是 **`uint8_t`**，按
`kk-oled-port/references/driver-contract.md` 的兼容门槛，**物理宽度不得超过 255**。
若把 `OLED_PHYSICAL_WIDTH` 直接设成 320，脏区列号会溢出。

因此保持面板原生 **物理 240 × 320**（宽度 240 ≤ 255、页数 40 ≤ 255、
缓冲 240×40 = 9600 ≤ 65535，全部满足门槛），再用图形核心的 `OLED_SetRotation(OLED_ROTATION_90)`
得到 **320 × 240 逻辑画布**。这样 **`graphics/` 下一个字节都不用改**。

核心的旋转映射为逻辑 `(x, y) → 物理 (px, py) = (239 - y, x)`。要在横屏面板上正立显示，
驱动就令面板坐标 `(X, Y) = (py, 239 - px)`：于是一个物理页（8 行 py）映射成面板上
8 列 × N 行的矩形，数据按 X 递增、Y 递增排列（与原工程 `LCD_Fill` 的顺序一致）。

### 3.2 IT 模式为什么返回 `OLED_UNSUPPORTED`

用户选择的是「阻塞 + DMA」。SPI 中断发送在本工程没有真实实现，按契约
「SDK 缺少真正的 IT 能力时应返回 `OLED_UNSUPPORTED`，不要用阻塞调用冒充异步」，
`OLED_DriverWriteIT()` 固定返回不支持。`KK_UI_REFRESH_MODE` 仍为默认的 `KK_UI_REFRESH_DMA`，
该分支不会被调用。

### 3.3 对比度为什么映射到背光 PWM

面板（ILI9341 兼容命令集）**没有对比度寄存器**。`OLED_DriverSetContrast()` 实现为
**PB1 / TIM3_CH4 背光 PWM**（1 kHz、1000 级占空比，映射 `value/255`）：

- 这是真实的屏幕亮度控制，不是只改菜单数值；
- 初始化即 100% 占空比 = 与原来 `LCD_BLK_Set()` 拉高 PB1 完全等价，不会出现黑屏风险；
- 标准 Demo 的「屏幕亮度」条目因此可正常工作，而不是被禁用。

若实机发现该模组背光是反相驱动，把 `kk_oled_driver.c` 里的
`oc.TIM_OCPolarity` 改成 `TIM_OCPolarity_Low` 即可。

### 3.4 1 ms 时基为什么用 TIM7

本工程 `SYSTEM/delay/delay.c` 在 `SYSTEM_SUPPORT_OS == 0` 时把 **SysTick 当作一次性忙等计数器**
使用（只操作 `LOAD/VAL/CTRL` 并反复开关 `ENABLE`），不能兼作周期节拍——若开启 `TICKINT`
会被这些忙等延时打出伪中断。KK_UI 需要单调递增的毫秒时基用于帧调度、动画、去抖重复与 FPS，
所以用完全空闲的 **TIM7**（基本定时器，无引脚、无其他占用）产生 1 ms 中断。

---

## 4. 新增与修改的文件

### 新增

```text
.agents/skills/                 kk-oled-port / kk-oled-use / kk-oled-font
                                kk-ui-port   / kk-ui-use   / kk-ui-extend
HARDWARE/KK_OLED/include/       kk_oled.h                    （运行时快照，未改）
HARDWARE/KK_OLED/graphics/      kk_oled.c 等 5 个文件          （运行时快照，未改）
HARDWARE/KK_OLED/driver/        kk_oled_driver.h / .c        ← 本工程新写的硬件适配层
HARDWARE/KK_UI/include/         kk_ui.h / kk_ui_config.h / kk_ui_draw.h
HARDWARE/KK_UI/src/             kk_ui.c 等 8 个文件            （布局已迁移到 320x240）
HARDWARE/KK_APP/                标准 Demo：应用、字模、图标、资源记录
HARDWARE/KEY/                   key.h / key.c                ← PB7/PB8/PB9 三键
SYSTEM/tick/                    tick.h / tick.c              ← TIM7 1 ms 时基
tools/check_layout.py           布局与类型上限几何自检
tools/check_fonts.py            字模覆盖（缺字）自检
USER/main.c                     重写：OLED_Init → KK_UI_AppReset → KK_UI_Init 主循环
USER/main_legacy_tft_demo.c.txt 原 TFT 演示 main.c 的备份，不参与编译
```

### 修改

| 文件 | 改动 |
|---|---|
| `USER/LCD.uvprojx` | 新增 `KK_OLED` / `KK_UI` / `KK_APP` 文件组；`SYSTEM` 组加 `tick.c`；`HARDWARE` 组加 `key.c`；`FWLIB` 组加 `spi.c`/`dma.c`/`tim.c`；include 路径增加新目录；移除空 README 组 |
| `USER/stm32f4xx_it.c` | 新增 `DMA1_Stream4_IRQHandler`，只做事件转发到 `OLED_DriverHandleMemTxComplete()` / `OLED_DriverHandleError()` |
| `HARDWARE/KK_UI/src/kk_ui_internal.h` | 布局常量由 128×64 迁移到 320×240，并新增首页/菜单/信息页/模态框的绘制常量 |
| `HARDWARE/KK_UI/src/kk_ui_home.c` `.menu.c` `.page.c` `.nav.c` `.dialog.c` | 把硬编码坐标换成上述常量；行内文字改为按行高垂直居中 |
| `HARDWARE/KK_APP/src/kk_ui_app.c` | 波形自定义页按 `OLED_GetWidth()/GetHeight()` 布局，不再写死 128×64 |

### 未改动（保留在原处，但已移出编译）

`HARDWARE/LCD/lcd.c`、`lcd_init.c`、`zk.c`、`lcdfont.h`、`pic.h`，以及空的
`lcd_spi.c/h`、`HARDWARE/SD/*`、`FATFS/*`。它们仍在磁盘上，只是不再参与构建。
`lcd_init.c` 里那份已验证的面板初始化命令表被复制进了新的 `kk_oled_driver.c`。

### 320×240 布局取值

| 常量 | 参考 128×64 | 本工程 320×240 |
|---|---|---|
| `KK_UI_SCREEN_WIDTH/HEIGHT` | 128 / 64 | **320 / 240** |
| 菜单标题高 / 行高 / 可见行 | 14 / 16 / 3 | **30 / 32 / 6** |
| 信息页标题高 | 16 | **32** |
| 页面转场视差 | 32 | **96** |
| 首页图标 | 32×32 @ y=8，间距 64 | **32×32 @ y=52，间距 88**（尺寸不变） |
| 模态框 | 118×61 全屏近似 | **304×224** |
| Toast | 高 16，最大宽 120 | **高 32，最大宽 300** |
| 字模 | 14/12/14 | **不变**（沿用配套字模，不换字号） |

---

## 5. 构建配置

- `Define` 保持 `STM32F40_41xxx,USE_STDPERIPH_DRIVER`，未新增宏。
  刷新模式用 `kk_ui_config.h` 的默认值 `KK_UI_REFRESH_DMA`（该头文件已是工程副本）。
- 新增 include 路径：`..\SYSTEM\tick`、`..\HARDWARE\KEY`、
  `..\HARDWARE\KK_OLED\{include,graphics,driver}`、`..\HARDWARE\KK_UI\{include,src}`、
  `..\HARDWARE\KK_APP\include`。
- 新增参与编译的 FWLIB 源文件：`stm32f4xx_spi.c`、`stm32f4xx_dma.c`、`stm32f4xx_tim.c`。

---

## 6. 验证结果（分层，不互相替代）

| 层级 | 状态 | 证据 |
|---|---|---|
| 1. 静态与构建 | **通过** | `UV4 -r` 全量重建：0 Error / 0 Warning；源/头文件/宏/include 路径确认已进入构建；DMA 通道与中断向量 `DMA1_Stream4_IRQHandler`、`TIM7_IRQHandler` 均存在于启动文件向量表 |
| 2. 几何与类型上限自检 | **通过（47 项）** | `python tools/check_layout.py`：直接从真实头文件解析常量，检查驱动兼容门槛、逻辑/物理尺寸一致性、菜单/首页/信息页/模态框/Toast 的越界与重叠 |
| 3. 字模覆盖自检 | **通过（16 项）** | `python tools/check_fonts.py`：把 `kk_oled_font.c` 的 `oled_find_glyph()` 等价移植后逐字查询，Demo 用到的 172 个不同字符全部命中，无缺字 |
| 4. 主机功能测试 | **未执行** | 本机没有主机 C 工具链（无 gcc/clang/MSVC），无法把图形核心编成 x86 跑假传输 |
| 5. 目标构建 / 烧录 | **仅构建** | 未烧录（未获授权，且无目标设备连接） |
| 6. 实物验收 | **未执行** | 需要实机 |

---

## 7. 未验证项与剩余风险

按影响从大到小：

1. **横屏正立方向**：MADCTL 0x78 与 0xA8 相差 180°。本工程按原工程「320×240 用 0x78」
   的既有取值选取。若实机画面上下颠倒，改 `kk_oled_driver.c` 里
   `#define OLED_MADCTL 0x78U` → `0xA8U` 即可（一处改动）。
   若出现**左右镜像**（理论上不应发生），则需重新核对旋转与 MADCTL 的组合。
2. **背光 PWM**：PB1 走 TIM3_CH4 是否被该模组的背光电路接受、极性是否为正，
   需要实机确认（见 3.3，已给出改法；初始化即全亮，最坏情况不会黑屏）。
3. **DMA 刷新实测**：SPI2 = 10.5 MHz（4 分频），整屏 153600 B 约 117 ms；单页窗口命令在 DMA 完成
   中断里阻塞发送。KK_UI 的 `KK_UI_DISPLAY_TIMEOUT_MS` 已放宽到 600 ms，理论余量充足，
   但未在实机上测量。
4. **按键抖动**：去抖与长按重复完全交给 KK_UI（20 ms 去抖 / 450 ms 首次重复），
   项目层只做原始电平归一化。未实测按键手感。
5. **板载字库芯片**：PB12 高电平会选中板载字库芯片。新驱动在空闲时 CS 为高，
   与旧驱动行为一致；不使用时总线空闲无时钟，不影响显示。若该芯片在空闲时干扰
   MISO，我们也不读 MISO（SPI 为全双工但只启用 TX DMA）。
6. **全屏首帧耗时**：`OLED_DriverInit()` 会写满 153600 B 清空显存（约 60 ms），
   保证不会残留上电随机画面。

---

## 8. 继续开发时的注意事项

### 编码必须保持 UTF-8 with BOM

`armcc` 默认按系统 ANSI(GBK) 解析源码。中文以 UTF-8 存储时，若没有 BOM，
多字节序列会被错配成转义字符，产生 `#8: missing closing quote` 之类的语法错误；
而且字符串字面量的字节必须保持 UTF-8，`OLED_DrawUTF8()` 才能正确解码中文。

工程内所有含非 ASCII 的 `.c/.h` 已写入 UTF-8 BOM。**新增或另存文件时请保持 BOM**，
文件内容请用 UTF-8 + BOM 保存。

### 重新运行自检

```powershell
cd <工程根目录>
python tools\check_layout.py     # 改过布局常量后必跑
python tools\check_fonts.py      # 改过界面文字后必跑
```

### 不要做的事

- 不要把 `.agents/skills/` 的资产目录直接当构建输入；工程用的副本在
  `HARDWARE/KK_OLED`、`HARDWARE/KK_UI`、`HARDWARE/KK_APP`，它们归工程所有、可以继续修改。
- 不要把旧的 `HARDWARE/LCD/*` 重新加回构建：它和新驱动会争用同一组 SPI2 引脚与 PB12/PC5/PB1。
- 不要修改 `KK_UI_SCREEN_WIDTH/HEIGHT` 而不改 KK_OLED 的旋转：
  `KK_UI_Init()` 会校验 `OLED_GetWidth()/GetHeight()` 并在不一致时返回 `KK_UI_CONFIG_ERROR`
  （此时 `main.c` 会停在心跳闪灯状态）。

---

## 9. 许可

`HARDWARE/KK_OLED`、`HARDWARE/KK_UI`、`HARDWARE/KK_APP` 各自目录内的 `LICENSE.txt`
为 MIT（青岛波特律动科技有限公司）。Demo 使用的字体来自 LEDFont Agent API，
其许可不受 MIT 覆盖，来源记录见 `HARDWARE/KK_APP/RESOURCES.md` 与 `font-resources.json`。


---

## 附录 A：上电诊断与故障定位

`USER/main.c` 默认打开 `APP_BOOT_DIAGNOSTICS`。确认硬件正常后改成 `0` 即可去掉。

**LED 短闪报码**（亮/灭各 400 ms）：

| 闪次数 | 含义 |
|---:|---|
| 1 | 时钟 / GPIO / 按键初始化完成 |
| 2 | `OLED_Init()` 成功 |
| 3 | `KK_UI_Init()` 成功，进入正常运行（之后 LED 每 0.5 s 翻转） |
| 5（循环） | `OLED_Init()` 失败：面板 / SPI 没起来 |
| 6（循环） | `KK_UI_Init()` 失败：多为逻辑尺寸校验不过 |
| 7（循环） | 刷新持续失败且 `KK_UI_RecoverDisplay()` 无效 |

**屏幕自检顺序**：

| 步骤 | 画面 | 证明什么 |
|---|---|---|
| A | 满屏白 0.8 s | 用**原工程完全相同的竖屏配置**（MADCTL 0x08、0x2A=0..239、0x2B=0..319）填充。只依赖硬件 SPI + 面板 + 接线 |
| B | 黑 0.3 s | |
| C | 满屏白 0.6 s | 改回横屏配置，走 KK_OLED 阻塞刷新，证明横屏坐标映射正确 |
| D | 黑底 + 白边框 + 左上白方块 0.6 s | 证明绘图坐标与裁剪 |
| E | KK_UI 图标首页 | 全链路 |

判读：

- **A 就全黑** → 问题在硬件 SPI 本身（时钟/模式/AF/接线），与横屏映射无关。
  先把 `kk_oled_driver.c` 的 `OLED_SPI_PRESCALER` 改成 `SPI_BaudRatePrescaler_8`（5.25 MHz）再试。
- **A 有白但 C/D 没有** → 硬件 SPI 通了，问题在本移植的横屏映射或 MADCTL。
- **A/C/D 都有但 E 没有** → 问题在 DMA 刷新路径。可把 `kk_ui_config.h` 的
  `KK_UI_REFRESH_MODE` 临时改成 `KK_UI_REFRESH_BLOCKING` 验证。
- **画面上下颠倒** → `kk_oled_driver.c` 的 `OLED_MADCTL` 由 `0x78U` 改成 `0xA8U`。
- **背光不亮** → `oled_backlight_init()` 的 `TIM_OCPolarity_High` 改成 `Low`。

## 附录 B：为什么驱动里的 SPI 等待都有超时

本工程没有看门狗。第一版 driver 里 `while (TXE == RESET)` / `while (BSY == SET)`
都是无上界死等，一旦 SPI 没起来，固件会彻底失去外部表现（LED 不闪、屏幕不亮、
也无法从返回码判断），只能靠猜。现在所有等待都有 `OLED_SPI_POLL_LIMIT` 上界，
超时置位 `oled_spi_fault` 并由 `OLED_DriverInit()` / `OLED_DriverWriteBlocking()` /
`OLED_DriverWriteDMA()` 转成 `OLED_ERROR`，再由 `main.c` 用 LED 报码。
驱动程序不允许出现无法观测的挂起。
