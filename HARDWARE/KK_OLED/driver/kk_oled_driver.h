#ifndef KK_OLED_DRIVER_H
#define KK_OLED_DRIVER_H

#include "kk_oled.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 本工程的物理面板为 240x320、16 bpp RGB565 的 SPI TFT（ILI9341 兼容命令集）。
 * 物理事实来自本工程自带并已验证的 HARDWARE/LCD/lcd_init.c 与 lcd_init.h。
 *
 * 图形核心采用 1 bit 页式帧缓冲，且 OLED_InternalGetTransferMinX/MaxX 的返回类型
 * 是 uint8_t，所以物理宽度不得超过 255。这里保持面板原生 240x320，再用核心的
 * 旋转 90 得到 320x240 的逻辑画布，因此 graphics/ 下的源码无需任何改动。
 */
#define OLED_PHYSICAL_WIDTH 240U
#define OLED_PHYSICAL_HEIGHT 320U
/** 每页 8 行像素，因此 320 行屏幕共有 40 页。 */
#define OLED_PHYSICAL_PAGES (OLED_PHYSICAL_HEIGHT / 8U)

/** 初始化面板、GPIO、SPI2、DMA 与背光，清空显存后点亮显示。 */
OLED_Status OLED_DriverInit(void);

/** 阻塞发送核心准备好的全部差异页。 */
OLED_Status OLED_DriverWriteBlocking(void);

/** 本工程未实现 SPI 中断发送，固定返回 OLED_UNSUPPORTED。 */
OLED_Status OLED_DriverWriteIT(void);

/** 页窗口命令由 CPU 发送，页数据使用 DMA1 Stream4 发送。 */
OLED_Status OLED_DriverWriteDMA(void);

/** 查询驱动异步状态机是否正在传输。 */
bool OLED_DriverIsBusy(void);

/** 设置背光亮度。面板没有对比度寄存器，这里映射到 TIM3_CH4 背光 PWM。 */
OLED_Status OLED_DriverSetContrast(uint8_t value);

/** 阻塞发送显示关闭 0x28 或显示开启 0x29。 */
OLED_Status OLED_DriverSetPowerSave(bool enable);

/*
 * 本工程自有的上电诊断接口，不属于 KK_OLED 契约，只在 bring-up 阶段使用。
 */

/**
 * 用指定 MADCTL 重新初始化面板并整屏填入一种 RGB565 颜色。
 * madctl 传 0x08U 时地址跨度回到竖屏（0x2A=0..239、0x2B=0..319），与原工程
 * 已验证的 USE_HORIZONTAL==0 配置完全一致，因此可以把"硬件 SPI 是否通"
 * 与"本移植的横屏坐标映射是否正确"这两件事分开判断。
 * 要求先成功执行过 OLED_Init()（外设已初始化）。
 * 调用后画布方向变成该 madctl，需要再调用 OLED_Init() 才能恢复横屏。
 */
OLED_Status OLED_DriverBringUpFill(uint16_t rgb565, uint8_t madctl);

/* 由应用的中断服务程序转发，不直接占用启动文件里的全局弱符号。 */
/** 处理一次 DMA 传输完成事件并推进异步状态机。 */
void OLED_DriverHandleMemTxComplete(void);

/** 终止当前异步传输，并向核心报告错误。 */
void OLED_DriverHandleError(void);

#endif
