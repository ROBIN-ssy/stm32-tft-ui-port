#ifndef __KEY_H
#define __KEY_H

#include "sys.h"

/*
 * 三个独立按键：按下接地、低电平有效，使用 STM32 内部上拉。
 *   UP   = PB7
 *   DOWN = PB8
 *   OK   = PB9
 *
 * KK_GetMask() 返回的位定义与 KK_UI 的 KK_UI_KEY_UP/DOWN/OK 完全一致，
 * 应用层可以直接把它填进 KK_UI_Input.keys。去抖与长按重复由 KK_UI 自己完成，
 * 这里只负责把 GPIO 原始电平归一化，不重复实现一套去抖。
 */
#define KEY_UP_PIN     GPIO_Pin_7
#define KEY_DOWN_PIN   GPIO_Pin_8
#define KEY_OK_PIN     GPIO_Pin_9

#define KEY_MASK_UP    0x01U
#define KEY_MASK_DOWN  0x02U
#define KEY_MASK_OK    0x04U

/** 把 PB7/PB8/PB9 配置为带内部上拉的输入。 */
void KEY_Init(void);

/** 读取三个按键的当前电平，按下时对应位置 1。 */
u8 KEY_GetMask(void);

#endif
