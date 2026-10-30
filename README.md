# 把 KK_OLED / KK_UI 移植到 STM32F407 + ILI9341

把一套**单色 1-bit 帧缓冲**的 OLED GUI 框架，移植到一块 **RGB565 彩色 SPI TFT** 上，
跑出 320×240 横屏界面。
详细的技术记录在 **[`KK_UI_README.md`](KK_UI_README.md)**，本文只是入口。

| 项目 | 结果 |
|---|---|
| MCU | STM32F407VETx（ST 标准外设库 SPL）+ Keil MDK 5.25 |
| 面板 | 240×320 SPI TFT，ILI9341 兼容命令集，RGB565 |
| 引脚 | SCK=PB13 MOSI=PB15 CS=PB12 DC=PC5 BLK=PB1（TIM3_CH4 PWM）｜ 按键 UP=PB7 DOWN=PB8 OK=PB9 |
| 编译 | **0 Error / 0 Warning**（ARMCC 5.06 update 6） |
| 占用 | Code 39706 B ／ RO-data 7290 B ／ RW-data 256 B ／ ZI-data 25408 B |
| 实机验证 | ⚠️ **未执行**（见下文「诚实的部分」） |

---

## 核心取舍：为什么不把画布改成 320 宽

KK_OLED 的 `OLED_InternalGetTransferMinX/MaxX()` 返回类型是 **`uint8_t`**。
按驱动契约，物理宽度不能超过 255 —— 宽度直接设成 320 会溢出。

两条路：改驱动（侵入 `graphics/` 每一处），还是绕开？

我选了绕开：

```
面板原生物理 240 × 320  （宽 240 ≤ 255，页数 40 ≤ 255，缓冲 240×40 = 9600 ≤ 65535，全部满足契约）
        ↓  OLED_SetRotation(OLED_ROTATION_90)
逻辑画布 320 × 240 横屏
```

结果是 **`graphics/` 下一个字节都没改**，只改了 `driver/` 里的旋转映射和物理尺寸宏；
KK_UI 的公共 API 也没动，只迁移了布局常量和各页 Draw 的绘制坐标。

旋转映射为逻辑 `(x, y) → 物理 (px, py) = (239 - y, x)`；
驱动再令面板坐标 `(X, Y) = (py, 239 - px)`，于是一个物理页（8 行 × N）正好映射成面板上
「8 列 × N 行」的矩形，数据按 X 递增、Y 递增排列 —— 与原工程 `LCD_Fill()` 的顺序一致。

> **一句话**：约束不该硬碰。把 320 变成"逻辑尺寸"，物理层保持 240，问题就消失了。

---

## 上电自检：把"没显示"拆成能定位的几种

黑屏是最难调的故障，因为你不知道是硬件、SPI、命令、还是坐标算错。
所以 `main.c` 里做了一套分级的自检（`APP_BOOT_DIAGNOSTICS = 1` 时启用）：

**LED 短闪报码**（亮/灭各 400 ms，方便数）

| 闪法 | 含义 |
|---|---|
| 1 次 | 时钟 / GPIO / 按键初始化完成 |
| 2 次 | `OLED_Init()` 成功 |
| 3 次 | `KK_UI_Init()` 成功，进入正常运行（之后每 0.5 s 翻转） |
| 5 次循环 | `OLED_Init()` 失败（面板 / SPI 没起来，带超时不死等） |
| 6 次循环 | `KK_UI_Init()` 失败（多为逻辑尺寸校验不过） |
| 7 次循环 | 刷新持续失败且恢复无效 |

**屏幕自检序列**

| 步骤 | 画面 | 证明了什么 |
|---|---|---|
| A | 满屏白 0.8 s（用**原工程完全相同的竖屏配置**：MADCTL 0x08、0x2A=0..239、0x2B=0..319） | 硬件 SPI、面板、接线是通的 |
| B | 黑 0.3 s | — |
| C | 满屏白 0.6 s（改回本移植的横屏配置，走 KK_OLED 阻塞刷新） | 横屏坐标映射正确 |
| D | 黑底 + 白边框 + 左上白方块 0.6 s | 绘图坐标与裁剪正确 |
| E | 交给 KK_UI，显示图标首页 | 框架跑起来了 |

**怎么用**：若 A 有白屏而 C/D 没有 → 问题在横屏映射；
若 A 就没有 → 问题在硬件 SPI 本身。硬件确认正常后把 `APP_BOOT_DIAGNOSTICS` 改成 0。

---

## 诚实的部分

`KK_UI_README.md` 第 1 节的状态表里，这三行我没打勾：

- **实机烧录 / 目视验收：未执行**
- 传输方式写的是「阻塞刷新 + SPI2 TX DMA 异步刷新」，但 **DMA 路径未经实机验证**
- IT 入口按契约返回 `OLED_UNSUPPORTED`

代码能编译、能在纸上推演通，但**没上过板子**。这一条留在 README 里，不删。

---

## 目录

```
stm32-tft-ui-port/
├── USER/            main.c（自检序列）、系统文件、stm32f4xx_conf.h
├── HARDWARE/
│   ├── KK_OLED/     KK_OLED 移植层：driver/（改写）+ graphics/（未改）
│   ├── KK_UI/       KK_UI 框架：布局常量迁移 + 各页 Draw 坐标
│   ├── KK_APP/      应用层：图标首页、字体、菜单
│   ├── LCD/         ILI9341 底层驱动（原工程）
│   ├── KEY/  LED/  SD/   按键、指示灯、SD 卡
├── SYSTEM/          delay / sys / tick / usart
├── FWLIB/           ST 标准外设库（ST 官方，保留以便直接编译）
├── CORE/            CMSIS 内核文件
├── FATFS/           文件系统（原工程）
├── tools/           check_fonts.py、check_layout.py（字体与布局自检脚本）
├── keilkilll.bat    清理 Keil 中间文件
└── KK_UI_README.md  ★ 完整移植记录：硬件事实表、取舍理由、已验证/未验证清单
```

---

## 编译

用 Keil MDK 打开 `USER/LCD.uvprojx`，ARMCC 5.06 update 6，直接 Build。
`OBJ/` 是构建产物，已在 `.gitignore` 中排除。

---

## 第三方与许可

`HARDWARE/KK_OLED/`、`HARDWARE/KK_UI/`、`HARDWARE/KK_APP/` 来自第三方库
**KK_OLED 0.1.0 / KK_UI 0.1.0**，各自目录下保留原始 `LICENSE.txt`，版权归原作者。
本仓库只包含移植改动部分。`FWLIB/`、`CORE/` 为 ST 官方库，遵循 ST 的许可条款。
