/*
 * KK_OLED + KK_UI 演示程序
 *   MCU   : STM32F407VETx（标准外设库，Keil MDK）
 *   面板  : 240x320 SPI TFT（ILI9341 兼容命令集，RGB565）
 *           SCK=PB13  MOSI=PB15  CS=PB12  DC=PC5  BLK=PB1（TIM3_CH4 背光 PWM）
 *   按键  : UP=PB7  DOWN=PB8  OK=PB9（按下接地，MCU 内部上拉）
 *
 * 逻辑画布是 320x240 横屏：KK_OLED 保持面板原生 240x320 的 1 bit 页式帧缓冲，
 * 由 kk_oled_driver.c 里的旋转 90 + 横屏 MADCTL 呈现为 320x240 逻辑坐标。
 * 该逻辑尺寸必须与 kk_ui_internal.h 的 KK_UI_SCREEN_WIDTH/HEIGHT 完全一致，
 * KK_UI_Init() 会校验并在不一致时返回 KK_UI_CONFIG_ERROR。
 *
 * ---------------------------------------------------------------------------
 * 上电诊断（APP_BOOT_DIAGNOSTICS = 1 时启用）
 *
 * LED 短闪报码（亮/灭各 400 ms，方便数）：
 *   1 次      : 时钟 / GPIO / 按键初始化完成
 *   2 次      : OLED_Init() 成功
 *   3 次      : KK_UI_Init() 成功，进入正常运行（之后 LED 每 0.5 s 翻转）
 *   5 次循环  : OLED_Init() 失败（面板 / SPI 没起来；等待已带超时不会死等）
 *   6 次循环  : KK_UI_Init() 失败（多为逻辑尺寸校验不过）
 *   7 次循环  : 刷新持续失败且恢复无效
 *
 * 屏幕上的自检顺序（每步之间有明显停顿，便于区分）：
 *   A  满屏白 0.8 s   —— 用原工程完全相同的竖屏配置（MADCTL 0x08、
 *                        0x2A=0..239、0x2B=0..319）整屏填白。
 *                        这一步证明了硬件 SPI、面板、接线都是通的。
 *   B  黑 0.3 s
 *   C  满屏白 0.6 s   —— 改回本移植的横屏配置，走 KK_OLED 的阻塞刷新路径，
 *                        这一步证明横屏坐标映射正确。
 *   D  黑底 + 白边框 + 左上白方块 0.6 s —— 证明绘图坐标与裁剪。
 *   E  交给 KK_UI，显示图标首页。
 *
 * 若 A 有白屏而 C/D 没有，问题在横屏映射；若 A 就没有，问题在硬件 SPI 本身。
 * 硬件确认正常后把 APP_BOOT_DIAGNOSTICS 改成 0 即可去掉闪灯与自检画面。
 * ---------------------------------------------------------------------------
 */

#include "delay.h"
#include "sys.h"
#include "led.h"
#include "key.h"
#include "tick.h"

#include "kk_oled.h"
#include "kk_ui.h"
#include "kk_ui_app.h"

/* 本工程 driver 提供的 bring-up 诊断接口。 */
#include "kk_oled_driver.h"

/* KK_UI 要求更新函数至少每 10 ms 调用一次；这里跑得更快以保证按键响应。 */
#define APP_LOOP_DELAY_MS 2U
/* 运行期心跳 LED 的半周期。 */
#define APP_LED_PERIOD_MS 500U

/* 上电诊断总开关。 */
#define APP_BOOT_DIAGNOSTICS 1
/* 显示层连续恢复失败多少次后停在闪灯报码状态。 */
#define APP_DISPLAY_FAIL_LIMIT 3U

/* 原工程已验证的竖屏 MADCTL（USE_HORIZONTAL == 0）。 */
#define APP_VENDOR_MADCTL 0x08U
#define APP_RGB565_WHITE  0xFFFFU
#define APP_RGB565_BLACK  0x0000U

/* LED 短闪报码：闪 N 次 = 到达第 N 个里程碑。 */
#define BOOT_MARK_CLOCK_OK     1U
#define BOOT_MARK_OLED_OK      2U
#define BOOT_MARK_UI_OK        3U
#define BOOT_MARK_OLED_FAIL    5U
#define BOOT_MARK_UI_FAIL      6U
#define BOOT_MARK_DISPLAY_FAIL 7U

/*
 * 用 delay_ms（SysTick 忙等）驱动 LED，不依赖 TIM7。
 * 这样即使时基或屏幕有问题，也能从"闪几次"判断固件走到了哪一步。
 */
static void boot_mark(u8 count)
{
	u8 i;

	for (i = 0U; i < count; ++i)
	{
		LED = 1;
		delay_ms(400U);
		LED = 0;
		delay_ms(400U);
	}
	delay_ms(800U);
}

#if APP_BOOT_DIAGNOSTICS
/*
 * 面板自检：走 KK_OLED 自己的阻塞刷新路径，不经过 KK_UI。
 * 用来把"横屏坐标映射"与"KK_UI / DMA 刷新调度"分开验证。
 */
static void panel_selftest(void)
{
	OLED_Fill();
	(void)OLED_Update();
	delay_ms(600U);

	OLED_Clear();
	OLED_DrawFrame(0, 0, OLED_GetWidth(), OLED_GetHeight());
	OLED_DrawBox(16, 16, 64, 64);
	(void)OLED_Update();
	delay_ms(600U);

	OLED_Clear();
	(void)OLED_Update();
	delay_ms(150U);
}
#endif

int main(void)
{
	KK_UI_Input input;
	u32 led_phase = 0U;
	u32 last_recover = 0U;
	u8 recover_count = 0U;

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	delay_init(168);
	LED_Init();
	KEY_Init();

	boot_mark(BOOT_MARK_CLOCK_OK);

	TICK_Init();

	if (OLED_Init() != OLED_OK)
	{
		/* 面板 / SPI 初始化失败；等待已经带超时，不会死等。 */
		while (1)
		{
			boot_mark(BOOT_MARK_OLED_FAIL);
		}
	}
	boot_mark(BOOT_MARK_OLED_OK);

#if APP_BOOT_DIAGNOSTICS
	/*
	 * A/B：原工程完全相同的竖屏配置整屏填白再填黑。
	 * 这一步只依赖硬件 SPI + 面板 + 接线，与本移植的横屏映射无关。
	 */
	(void)OLED_DriverBringUpFill(APP_RGB565_WHITE, APP_VENDOR_MADCTL);
	delay_ms(800U);
	(void)OLED_DriverBringUpFill(APP_RGB565_BLACK, APP_VENDOR_MADCTL);
	delay_ms(300U);

	/* 上一步把面板改成竖屏了，重新初始化回本移植的横屏配置。 */
	if (OLED_Init() != OLED_OK)
	{
		while (1)
		{
			boot_mark(BOOT_MARK_OLED_FAIL);
		}
	}

	/* C/D：走 KK_OLED 自己的路径，验证横屏坐标映射与绘图坐标。 */
	panel_selftest();
#endif

	/* 标准 Demo 规定的顺序：OLED_Init() -> KK_UI_AppReset() -> KK_UI_Init()。 */
	KK_UI_AppReset(TICK_GetMs());
	if (KK_UI_Init(KK_UI_AppGet()) != KK_UI_OK)
	{
		while (1)
		{
			boot_mark(BOOT_MARK_UI_FAIL);
		}
	}
	boot_mark(BOOT_MARK_UI_OK);

	while (1)
	{
		u32 now = TICK_GetMs();
		KK_UI_Status status;

		/* 去抖与长按重复由 KK_UI 负责，这里只提供归一化后的原始按键状态。 */
		input.keys = KEY_GetMask();
		input.encoder_delta = 0;

		/* 三步顺序固定，不可颠倒。 */
		KK_UI_AppUpdate(now);
		status = KK_UI_Update(now, input);
		KK_UI_AppProcessEvents();

		if (status != KK_UI_OK)
		{
			/*
			 * 显示层出错（传输超时或传输失败）。这里尝试恢复，并在多次恢复
			 * 无效时用 LED 报码停机，避免出现"屏幕黑着但程序看着像在跑"
			 * 这种无法判断的状态。
			 */
			if ((now - last_recover) >= 1000U)
			{
				last_recover = now;
				++recover_count;
				(void)KK_UI_RecoverDisplay();
				if (recover_count >= APP_DISPLAY_FAIL_LIMIT)
				{
					while (1)
					{
						boot_mark(BOOT_MARK_DISPLAY_FAIL);
					}
				}
			}
		}
		else
		{
			recover_count = 0U;
		}

		if ((now / APP_LED_PERIOD_MS) != led_phase)
		{
			led_phase = now / APP_LED_PERIOD_MS;
			LED = !LED;
		}

		delay_ms(APP_LOOP_DELAY_MS);
	}
}
