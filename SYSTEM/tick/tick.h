#ifndef __TICK_H
#define __TICK_H

#include "sys.h"

/*
 * KK_UI 需要一个单调递增的毫秒时基，用于帧调度、动画、按键去抖重复和 FPS 统计。
 *
 * 本工程的 delay.c 在 SYSTEM_SUPPORT_OS=0 时把 SysTick 当成一次性忙等计数器使用
 * （只操作 LOAD/VAL/CTRL，并反复开关 ENABLE），因此 SysTick 不能再兼作周期节拍。
 * 这里改用完全空闲的 TIM7（基本定时器，无引脚、无其他占用）产生 1 ms 中断。
 */

/** 启动 TIM7 的 1 ms 周期中断。 */
void TICK_Init(void);

/** 读取自 TICK_Init() 以来经过的毫秒数，32 位回绕即可。 */
u32 TICK_GetMs(void);

#endif
