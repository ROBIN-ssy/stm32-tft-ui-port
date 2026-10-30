#include "tick.h"

/* TIM7 在 APB1 上；APB1 分频不为 1 时定时器时钟为 84 MHz。 */
#define TICK_TIM          TIM7
#define TICK_TIM_CLK      RCC_APB1Periph_TIM7
#define TICK_PRESCALER    (84U - 1U)
#define TICK_PERIOD       (1000U - 1U)
#define TICK_IRQn         TIM7_IRQn

static volatile u32 tick_ms;

void TICK_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(TICK_TIM_CLK, ENABLE);

	TIM_TimeBaseStructure.TIM_Prescaler = TICK_PRESCALER;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period = TICK_PERIOD;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TICK_TIM, &TIM_TimeBaseStructure);

	tick_ms = 0U;
	TIM_ClearITPendingBit(TICK_TIM, TIM_IT_Update);
	TIM_ITConfig(TICK_TIM, TIM_IT_Update, ENABLE);

	/* 优先级低于显示 DMA，避免时基打断正在推进的刷新状态机。 */
	NVIC_InitStructure.NVIC_IRQChannel = TICK_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TICK_TIM, ENABLE);
}

u32 TICK_GetMs(void)
{
	/* Cortex-M4 上的 32 位对齐读本身是原子的。 */
	return tick_ms;
}

void TIM7_IRQHandler(void)
{
	if (TIM_GetITStatus(TICK_TIM, TIM_IT_Update) != RESET) {
		TIM_ClearITPendingBit(TICK_TIM, TIM_IT_Update);
		++tick_ms;
	}
}
