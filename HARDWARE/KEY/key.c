#include "key.h"

/* 三个按键共用 GPIOB，硬件接线与有效电平是这一层唯一的适配边界。 */

void KEY_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Pin = KEY_UP_PIN | KEY_DOWN_PIN | KEY_OK_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; /* 按下接 GND，读 0 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

u8 KEY_GetMask(void)
{
	u8 mask = 0U;

	if (GPIO_ReadInputDataBit(GPIOB, KEY_UP_PIN) == Bit_RESET) {
		mask |= KEY_MASK_UP;
	}
	if (GPIO_ReadInputDataBit(GPIOB, KEY_DOWN_PIN) == Bit_RESET) {
		mask |= KEY_MASK_DOWN;
	}
	if (GPIO_ReadInputDataBit(GPIOB, KEY_OK_PIN) == Bit_RESET) {
		mask |= KEY_MASK_OK;
	}
	return mask;
}
