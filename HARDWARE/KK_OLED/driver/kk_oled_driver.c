/*
 * KK_OLED 具体 driver：STM32F4 标准外设库 + SPI2 + DMA1 Stream4 + 240x320 RGB565 TFT。
 *
 * 本文件是唯一的硬件适配边界。所有平台事实都集中在这里，graphics/ 与 KK_UI 不感知硬件。
 *
 * 引脚与控制器事实来自本工程已有且已验证的 HARDWARE/LCD/lcd_init.c、lcd_init.h：
 *   SPI2  SCK = PB13   MISO = PB14(不使用)   MOSI = PB15
 *   CS        = PB12  低电平选中 LCD（该脚同时接到板载字库芯片，高电平选字库）
 *   DC        = PC5
 *   BLK       = PB1   TIM3_CH4 背光 PWM
 *   初始化命令表沿用工程原有序列，仅把 MADCTL 换成横屏取值。
 *
 * 1 bit 页帧缓冲到 16 bpp 面板的映射：
 *   核心旋转 90 后，逻辑 (x, y) 对应物理页缓冲 (px, py) = (239 - y, x)；
 *   面板使用横屏 MADCTL，地址空间 (0x2A = X in [0,320), 0x2B = Y in [0,240))；
 *   要让逻辑画面正立，就令面板 (X, Y) = (py, 239 - px)。
 *   于是一个物理页（8 行 py）映射为面板上 8 列 x N 行的矩形，数据按 X 递增、
 *   Y 递增排列（与原工程 LCD_Fill 的 x 内层、y 外层顺序一致）。
 */

#include "kk_oled_driver.h"

#include "kk_oled_internal.h"

#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_spi.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_tim.h"
#include "delay.h"

#include <string.h>

/* ============================ 硬件事实 ============================ */

#define OLED_SPI              SPI2
#define OLED_SPI_CLK          RCC_APB1Periph_SPI2
/*
 * APB1 = 42 MHz，4 分频得到 10.5 MHz。
 * 这类模组常用杜邦线连接，21 MHz 下容易采样失败并表现为"完全没有画面"，
 * 因此首次上电先用 10.5 MHz。整屏 153600B 约 117 ms，仍在 KK_UI 的 250 ms
 * 显示超时之内。确认硬件稳定后可改为 SPI_BaudRatePrescaler_2（21 MHz）。
 */
#define OLED_SPI_PRESCALER    SPI_BaudRatePrescaler_4

#define OLED_DMA_STREAM       DMA1_Stream4
#define OLED_DMA_CHANNEL      DMA_Channel_0
#define OLED_DMA_IRQn         DMA1_Stream4_IRQn
#define OLED_DMA_FLAGS        (DMA_FLAG_TCIF4 | DMA_FLAG_TEIF4 | \
                               DMA_FLAG_DMEIF4 | DMA_FLAG_FEIF4)
#define OLED_DMA_ITS          (DMA_IT_TCIF4 | DMA_IT_TEIF4)

#define OLED_CS_PORT          GPIOB
#define OLED_CS_PIN           GPIO_Pin_12
#define OLED_DC_PORT          GPIOC
#define OLED_DC_PIN           GPIO_Pin_5
#define OLED_BLK_PORT         GPIOB
#define OLED_BLK_PIN          GPIO_Pin_1
#define OLED_BLK_PIN_SOURCE   GPIO_PinSource1
#define OLED_BLK_AF           GPIO_AF_TIM3
#define OLED_BLK_TIM          TIM3
#define OLED_BLK_TIM_CLK      RCC_APB1Periph_TIM3
/** TIM3 挂在 APB1 上，APB1 分频不为 1 时定时器时钟为 84 MHz。 */
#define OLED_BLK_TIM_PRESCALER (84U - 1U)
/** 1 kHz PWM，占空比分辨率 1000 级。 */
#define OLED_BLK_TIM_PERIOD   (1000U - 1U)
#define OLED_BLK_FULL_DUTY    (1000U)

/** 面板方向。0x78 与 0xA8 都是本模组的横屏取值，两者相差 180°。 */
#define OLED_MADCTL           0x78U

/** 逻辑画布方向：把 240x320 物理缓冲呈现为 320x240 逻辑画布。 */
#define OLED_CANVAS_ROTATION  OLED_ROTATION_90

/** 单色 1 bit 到 RGB565 的两种取值；黑白在 RGB/BGR 下相同。 */
#define OLED_COLOR_ON         0xFFFFU
#define OLED_COLOR_OFF        0x0000U

/** 面板上电稳定等待时间。 */
#define OLED_RESET_DELAY_MS   20U

#define OLED_CS_CLR()         GPIO_ResetBits(OLED_CS_PORT, OLED_CS_PIN)
#define OLED_CS_SET()         GPIO_SetBits(OLED_CS_PORT, OLED_CS_PIN)
#define OLED_DC_CLR()         GPIO_ResetBits(OLED_DC_PORT, OLED_DC_PIN)
#define OLED_DC_SET()         GPIO_SetBits(OLED_DC_PORT, OLED_DC_PIN)

/** 单个物理页展开后的最大字节数：8 列 x 240 行 x 2 字节。 */
#define OLED_STAGING_SIZE     ((uint32_t)OLED_PHYSICAL_WIDTH * 8U * 2U)

/*
 * 横屏 MADCTL 下面板的地址跨度与竖屏相反：
 *   0x2A 走长边（= 物理高度 320），0x2B 走短边（= 物理宽度 240）。
 * 逐页映射里 X = py 落在 [0,320)、Y = 239 - px 落在 [0,240)，与此一致。
 */
#define OLED_PANEL_COLS       OLED_PHYSICAL_HEIGHT
#define OLED_PANEL_ROWS       OLED_PHYSICAL_WIDTH

/** 当前异步刷新采用的数据发送方式。 */
typedef enum {
    OLED_DRIVER_MODE_NONE = 0, /**< 空闲或正在阻塞调用。 */
    OLED_DRIVER_MODE_DMA       /**< 页数据由 DMA 发送。 */
} OLED_DriverMode;

static uint8_t oled_staging[OLED_STAGING_SIZE]; /**< 页展开缓冲，DMA 源。 */
static volatile uint16_t oled_staging_len;      /**< 本次 DMA 的字节数。 */
static volatile bool oled_driver_busy;          /**< 驱动状态机是否占用总线。 */
static volatile uint8_t oled_driver_page;       /**< 当前正在发送的物理页。 */
static volatile OLED_DriverMode oled_driver_mode; /**< 当前异步模式。 */

static uint16_t oled_win_x1; /**< 面板窗口起始列（= 物理行 py 起点）。 */
static uint16_t oled_win_x2; /**< 面板窗口结束列。 */
static uint16_t oled_win_y1; /**< 面板窗口起始行。 */
static uint16_t oled_win_y2; /**< 面板窗口结束行。 */

/* ============================ 低层 SPI ============================ */

/*
 * 所有等待 SPI 标志的循环都必须有上界。本工程没有看门狗，一个死等的 while
 * 会让固件彻底失去外部表现（LED 不闪、屏幕不亮、也无从用返回码定位），
 * 排查代价极高。超时后置位 oled_spi_fault，由上层转换成 OLED_ERROR。
 */
#define OLED_SPI_POLL_LIMIT 0x00080000UL

/** 低层 SPI 等待一旦超时就置位，直到下次初始化/刷新显式清除。 */
static volatile bool oled_spi_fault;

static void oled_spi_write_byte(uint8_t value)
{
    uint32_t guard = OLED_SPI_POLL_LIMIT;

    if (oled_spi_fault) {
        return;
    }
    while (SPI_I2S_GetFlagStatus(OLED_SPI, SPI_I2S_FLAG_TXE) == RESET) {
        if (--guard == 0U) {
            oled_spi_fault = true;
            return;
        }
    }
    SPI_I2S_SendData(OLED_SPI, value);
}

static void oled_spi_wait_idle(void)
{
    uint32_t guard = OLED_SPI_POLL_LIMIT;

    if (oled_spi_fault) {
        return;
    }
    while (SPI_I2S_GetFlagStatus(OLED_SPI, SPI_I2S_FLAG_BSY) == SET) {
        if (--guard == 0U) {
            oled_spi_fault = true;
            return;
        }
    }
}

/** 发送一个命令字节。调用者负责 CS 已经拉低。 */
static void oled_cmd(uint8_t command)
{
    OLED_DC_CLR();
    oled_spi_write_byte(command);
    oled_spi_wait_idle();
    OLED_DC_SET();
}

/** 发送一个数据字节。调用者负责 CS 已经拉低。 */
static void oled_data1(uint8_t value)
{
    OLED_DC_SET();
    oled_spi_write_byte(value);
    oled_spi_wait_idle();
}

/** 发送一个独立命令，自带 CS 拉低与拉高。 */
static void oled_cmd_cs(uint8_t command)
{
    OLED_CS_CLR();
    oled_cmd(command);
    oled_spi_wait_idle();
    OLED_CS_SET();
}

/** 设置面板显存地址窗口并进入写显存状态。 */
static void oled_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    oled_cmd(0x2AU);
    oled_data1((uint8_t)(x1 >> 8));
    oled_data1((uint8_t)(x1 & 0xFFU));
    oled_data1((uint8_t)(x2 >> 8));
    oled_data1((uint8_t)(x2 & 0xFFU));
    oled_cmd(0x2BU);
    oled_data1((uint8_t)(y1 >> 8));
    oled_data1((uint8_t)(y1 & 0xFFU));
    oled_data1((uint8_t)(y2 >> 8));
    oled_data1((uint8_t)(y2 & 0xFFU));
    oled_cmd(0x2CU);
}

/* ============================ 页展开 ============================ */

/** 从 first 开始寻找下一个存在差异区间的物理页。 */
static bool oled_find_next_page(uint8_t first, uint8_t *page)
{
    uint8_t candidate;

    for (candidate = first; candidate < (uint8_t)OLED_PHYSICAL_PAGES; ++candidate) {
        if (OLED_InternalGetTransferMinX(candidate) < OLED_PHYSICAL_WIDTH) {
            *page = candidate;
            return true;
        }
    }
    return false;
}

/**
 * 把一个物理页的连续差异区间展开成面板窗口里的 RGB565 像素。
 * buffer 是核心冻结的传输帧，本函数同时填好窗口坐标与 oled_staging。
 */
static void oled_prepare_page(uint8_t page, const uint8_t *buffer)
{
    uint8_t min_x = OLED_InternalGetTransferMinX(page); /**< 本页首个差异列。 */
    uint8_t max_x = OLED_InternalGetTransferMaxX(page); /**< 本页最后差异列。 */
    uint16_t base = (uint16_t)page * OLED_PHYSICAL_WIDTH; /**< 本页首字节。 */
    uint16_t x;
    uint16_t y;
    uint32_t out = 0U;

    /* 物理行 py 的 8 个取值直接成为面板的 8 个连续列。 */
    oled_win_x1 = (uint16_t)page * 8U;
    oled_win_x2 = (uint16_t)(oled_win_x1 + 7U);
    /* 物理列 px 越大，面板行 Y = 239 - px 越小。 */
    oled_win_y1 = (uint16_t)(OLED_PHYSICAL_WIDTH - 1U - max_x);
    oled_win_y2 = (uint16_t)(OLED_PHYSICAL_WIDTH - 1U - min_x);

    for (x = oled_win_x1; x <= oled_win_x2; ++x) {
        uint8_t mask = (uint8_t)(1U << (uint8_t)(x & 7U)); /* 该列对应的页内位。 */
        for (y = oled_win_y1; y <= oled_win_y2; ++y) {
            uint8_t cell = buffer[base + (uint16_t)(OLED_PHYSICAL_WIDTH - 1U - y)];
            uint16_t color = ((cell & mask) != 0U) ? OLED_COLOR_ON : OLED_COLOR_OFF;

            oled_staging[out++] = (uint8_t)(color >> 8);
            oled_staging[out++] = (uint8_t)(color & 0xFFU);
        }
    }
    oled_staging_len = (uint16_t)out;
}

/* ============================ 外设初始化 ============================ */

static void oled_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC, ENABLE);

    /* PB13 = SPI2_SCK, PB15 = SPI2_MOSI, 复用推挽。PB14(MISO) 保持复位默认输入。 */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF_SPI2);
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &gpio);

    /* PB12 = CS，PC5 = DC。 */
    gpio.GPIO_Pin = OLED_CS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(OLED_CS_PORT, &gpio);

    gpio.GPIO_Pin = OLED_DC_PIN;
    GPIO_Init(OLED_DC_PORT, &gpio);

    OLED_CS_SET();
    OLED_DC_SET();

    /* PB1 = TIM3_CH4 背光 PWM。 */
    GPIO_PinAFConfig(OLED_BLK_PORT, OLED_BLK_PIN_SOURCE, OLED_BLK_AF);
    gpio.GPIO_Pin = OLED_BLK_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(OLED_BLK_PORT, &gpio);
}

static void oled_spi_init(void)
{
    SPI_InitTypeDef spi;

    RCC_APB1PeriphClockCmd(OLED_SPI_CLK, ENABLE);
    SPI_I2S_DeInit(OLED_SPI);

    /* 只向面板写数据。这里刻意使用无歧义的全双工方向：1 线双向模式下数据线
     * 归属在 STM32 文档中的表述容易被误读，配错会导致完全没有输出。
     * 接收通道溢出（OVR）不影响发送，而且本项目只启用 TX DMA。 */
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = OLED_SPI_PRESCALER;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(OLED_SPI, &spi);
    /*
     * 软件 NSS 的主模式必须保持 SSI=1，否则会被判为 Mode Fault 并清掉 SPE。
     * 本 SPL 的 SPI_Mode_Master 宏已经是 0x0104（含 SSI），这里再显式置一次，
     * 避免将来更换 SPL 版本或修改宏定义时静默失效。
     */
    OLED_SPI->CR1 |= SPI_CR1_SSI;
    SPI_Cmd(OLED_SPI, ENABLE);
}

static void oled_dma_init(void)
{
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    DMA_DeInit(OLED_DMA_STREAM);
    DMA_StructInit(&dma);
    dma.DMA_Channel = OLED_DMA_CHANNEL;
    dma.DMA_PeripheralBaseAddr = (uint32_t)(&(OLED_SPI->DR));
    dma.DMA_Memory0BaseAddr = (uint32_t)oled_staging;
    dma.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dma.DMA_BufferSize = 1U;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_FIFOMode = DMA_FIFOMode_Disable;
    dma.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    dma.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    dma.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(OLED_DMA_STREAM, &dma);
    DMA_ClearFlag(OLED_DMA_STREAM, OLED_DMA_FLAGS);

    nvic.NVIC_IRQChannel = OLED_DMA_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

static void oled_backlight_init(void)
{
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef oc;

    RCC_APB1PeriphClockCmd(OLED_BLK_TIM_CLK, ENABLE);

    time_base.TIM_Prescaler = OLED_BLK_TIM_PRESCALER;
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    time_base.TIM_Period = OLED_BLK_TIM_PERIOD;
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    time_base.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(OLED_BLK_TIM, &time_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = OLED_BLK_FULL_DUTY; /* 上电即全亮，避免误判为黑屏。 */
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC4Init(OLED_BLK_TIM, &oc);
    TIM_OC4PreloadConfig(OLED_BLK_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(OLED_BLK_TIM, ENABLE);
    TIM_Cmd(OLED_BLK_TIM, ENABLE);
}

/* ============================ 面板初始化 ============================ */

/**
 * 按给定地址跨度整屏填充一种 RGB565 颜色。
 * 跨度与 MADCTL 必须匹配：MV=1（横屏）时 0x2A 走长边 320，否则走 240。
 */
static bool oled_fill_gram(uint16_t rgb565, uint16_t cols, uint16_t rows)
{
    const uint32_t total = (uint32_t)cols * rows * 2U;
    uint32_t sent = 0U;
    uint16_t i;

    for (i = 0U; i < (uint16_t)sizeof(oled_staging); i += 2U) {
        oled_staging[i] = (uint8_t)(rgb565 >> 8);
        oled_staging[i + 1U] = (uint8_t)(rgb565 & 0xFFU);
    }
    oled_set_window(0U, 0U, (uint16_t)(cols - 1U), (uint16_t)(rows - 1U));
    OLED_DC_SET();
    while (sent < total) {
        uint32_t chunk = total - sent;
        uint32_t j;

        if (chunk > sizeof(oled_staging)) {
            chunk = sizeof(oled_staging);
        }
        for (j = 0U; j < chunk; ++j) {
            oled_spi_write_byte(oled_staging[j]);
        }
        sent += chunk;
    }
    oled_spi_wait_idle();
    return !oled_spi_fault;
}

/** 发送本模组已验证的初始化命令表；madctl 决定横屏还是竖屏。 */
static bool oled_panel_init(uint8_t madctl)
{
    OLED_CS_CLR();

    oled_cmd(0x11U); /* Sleep out */
    delay_ms(120U);

    oled_cmd(0xCFU); oled_data1(0x00U); oled_data1(0xC1U); oled_data1(0x30U);
    oled_cmd(0xEDU); oled_data1(0x64U); oled_data1(0x03U);
    oled_data1(0x12U); oled_data1(0x81U);
    oled_cmd(0xE8U); oled_data1(0x85U); oled_data1(0x00U); oled_data1(0x79U);
    oled_cmd(0xCBU); oled_data1(0x39U); oled_data1(0x2CU); oled_data1(0x00U);
    oled_data1(0x34U); oled_data1(0x02U);
    oled_cmd(0xF7U); oled_data1(0x20U);
    oled_cmd(0xEAU); oled_data1(0x00U); oled_data1(0x00U);
    oled_cmd(0xC0U); oled_data1(0x1DU); /* Power control */
    oled_cmd(0xC1U); oled_data1(0x12U);
    oled_cmd(0xC5U); oled_data1(0x33U); oled_data1(0x3FU);
    oled_cmd(0xC7U); oled_data1(0x92U);
    oled_cmd(0x3AU); oled_data1(0x55U); /* 16 bit/pixel = RGB565 */
    oled_cmd(0x36U); oled_data1(madctl);
    oled_cmd(0xB1U); oled_data1(0x00U); oled_data1(0x12U);
    oled_cmd(0xB6U); oled_data1(0x0AU); oled_data1(0xA2U);
    oled_cmd(0x44U); oled_data1(0x02U);

    oled_cmd(0xF2U); oled_data1(0x00U); /* 3Gamma Function Disable */
    oled_cmd(0x26U); oled_data1(0x01U); /* Gamma curve selected */
    oled_cmd(0xE0U);
    oled_data1(0x0FU); oled_data1(0x22U); oled_data1(0x1CU); oled_data1(0x1BU);
    oled_data1(0x08U); oled_data1(0x0FU); oled_data1(0x48U); oled_data1(0xB8U);
    oled_data1(0x34U); oled_data1(0x05U); oled_data1(0x0CU); oled_data1(0x09U);
    oled_data1(0x0FU); oled_data1(0x07U); oled_data1(0x00U);
    oled_cmd(0xE1U);
    oled_data1(0x00U); oled_data1(0x23U); oled_data1(0x24U); oled_data1(0x07U);
    oled_data1(0x10U); oled_data1(0x07U); oled_data1(0x38U); oled_data1(0x47U);
    oled_data1(0x4BU); oled_data1(0x0AU); oled_data1(0x13U); oled_data1(0x06U);
    oled_data1(0x30U); oled_data1(0x38U); oled_data1(0x0FU);

    (void)oled_fill_gram(0x0000U, OLED_PANEL_COLS, OLED_PANEL_ROWS);

    oled_cmd(0x29U); /* Display on */
    oled_spi_wait_idle();
    OLED_CS_SET();
    return !oled_spi_fault;
}

/* ============================ 异步状态机 ============================ */

/** 释放异步状态并把最终结果交回图形核心。 */
static void oled_finish_async(OLED_Status status)
{
    DMA_ITConfig(OLED_DMA_STREAM, OLED_DMA_ITS, DISABLE);
    oled_driver_busy = false;
    oled_driver_mode = OLED_DRIVER_MODE_NONE;
    OLED_InternalTransferFinished(status);
}

/** 启动一页的 DMA 发送；调用前核心帧必须已经冻结。 */
static void oled_start_page_async(uint8_t page)
{
    const uint8_t *buffer = OLED_InternalGetTransferBuffer();

    oled_prepare_page(page, buffer);
    oled_driver_page = page;

    OLED_CS_CLR();
    oled_set_window(oled_win_x1, oled_win_y1, oled_win_x2, oled_win_y2);
    OLED_DC_SET();

    DMA_Cmd(OLED_DMA_STREAM, DISABLE);
    while (DMA_GetCmdStatus(OLED_DMA_STREAM) != DISABLE) {
    }
    DMA_ClearFlag(OLED_DMA_STREAM, OLED_DMA_FLAGS);
    DMA_SetCurrDataCounter(OLED_DMA_STREAM, oled_staging_len);
    DMA_ITConfig(OLED_DMA_STREAM, OLED_DMA_ITS, ENABLE);
    SPI_I2S_DMACmd(OLED_SPI, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(OLED_DMA_STREAM, ENABLE);
}

/* ============================ 公共接口 ============================ */

OLED_Status OLED_DriverInit(void)
{
    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;
    oled_driver_mode = OLED_DRIVER_MODE_NONE;
    oled_staging_len = 0U;
    oled_spi_fault = false;

    oled_gpio_init();
    oled_spi_init();
    oled_dma_init();
    oled_backlight_init();

    delay_ms(OLED_RESET_DELAY_MS);

    /*
     * 面板初始化失败说明 SPI 没有真正跑起来。必须报错而不是假装成功，
     * 否则上层会认为屏幕可用，最终只表现为"背光亮但永远没有画面"。
     */
    if (!oled_panel_init(OLED_MADCTL)) {
        oled_driver_busy = false;
        return OLED_ERROR;
    }

    /* 逻辑画布方向必须在这里确定，OLED_Init 已把旋转复位成 0。 */
    OLED_SetRotation(OLED_CANVAS_ROTATION);

    oled_driver_busy = false;
    return OLED_OK;
}

OLED_Status OLED_DriverWriteBlocking(void)
{
    const uint8_t *buffer;
    uint8_t page;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;
    oled_driver_mode = OLED_DRIVER_MODE_NONE;
    oled_spi_fault = false;
    buffer = OLED_InternalGetTransferBuffer();

    for (page = 0U; page < (uint8_t)OLED_PHYSICAL_PAGES; ++page) {
        uint32_t i;

        if (OLED_InternalGetTransferMinX(page) >= OLED_PHYSICAL_WIDTH) {
            continue;
        }
        oled_prepare_page(page, buffer);

        OLED_CS_CLR();
        oled_set_window(oled_win_x1, oled_win_y1, oled_win_x2, oled_win_y2);
        OLED_DC_SET();
        for (i = 0U; i < (uint32_t)oled_staging_len; ++i) {
            oled_spi_write_byte(oled_staging[i]);
        }
        oled_spi_wait_idle();
        OLED_CS_SET();
    }

    oled_driver_busy = false;
    return oled_spi_fault ? OLED_ERROR : OLED_OK;
}

OLED_Status OLED_DriverWriteIT(void)
{
    /* 本移植只实现了阻塞与 DMA；契约要求未实现的能力明确返回不支持。 */
    return OLED_UNSUPPORTED;
}

OLED_Status OLED_DriverWriteDMA(void)
{
    uint8_t first_page;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    if (!oled_find_next_page(0U, &first_page)) {
        return OLED_OK;
    }

    /* 状态必须在启动 DMA 之前写好，完成回调才能正确识别当前模式。 */
    oled_driver_busy = true;
    oled_driver_mode = OLED_DRIVER_MODE_DMA;
    oled_spi_fault = false;
    oled_start_page_async(first_page);
    if (oled_spi_fault) {
        /* 页窗口命令都没发出去，不必等 DMA 完成；立刻释放状态并报错。 */
        SPI_I2S_DMACmd(OLED_SPI, SPI_I2S_DMAReq_Tx, DISABLE);
        DMA_Cmd(OLED_DMA_STREAM, DISABLE);
        DMA_ITConfig(OLED_DMA_STREAM, OLED_DMA_ITS, DISABLE);
        OLED_CS_SET();
        oled_driver_busy = false;
        oled_driver_mode = OLED_DRIVER_MODE_NONE;
        return OLED_ERROR;
    }
    return OLED_OK;
}

bool OLED_DriverIsBusy(void)
{
    return oled_driver_busy;
}

OLED_Status OLED_DriverSetContrast(uint8_t value)
{
    uint32_t duty;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    duty = ((uint32_t)value * OLED_BLK_FULL_DUTY) / 255U;
    TIM_SetCompare4(OLED_BLK_TIM, (uint16_t)duty);
    return OLED_OK;
}

OLED_Status OLED_DriverBringUpFill(uint16_t rgb565, uint8_t madctl)
{
    const uint16_t cols = ((madctl & 0x20U) != 0U) ? OLED_PHYSICAL_HEIGHT
                                                  : OLED_PHYSICAL_WIDTH;
    const uint16_t rows = ((madctl & 0x20U) != 0U) ? OLED_PHYSICAL_WIDTH
                                                  : OLED_PHYSICAL_HEIGHT;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;
    oled_driver_mode = OLED_DRIVER_MODE_NONE;
    oled_spi_fault = false;

    if (!oled_panel_init(madctl)) {
        oled_driver_busy = false;
        return OLED_ERROR;
    }
    (void)oled_fill_gram(rgb565, cols, rows);

    oled_driver_busy = false;
    return oled_spi_fault ? OLED_ERROR : OLED_OK;
}

OLED_Status OLED_DriverSetPowerSave(bool enable)
{
    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_spi_fault = false;
    oled_cmd_cs(enable ? 0x28U : 0x29U); /* 0x28 关显示，0x29 开显示 */
    return oled_spi_fault ? OLED_ERROR : OLED_OK;
}

void OLED_DriverHandleMemTxComplete(void)
{
    uint8_t next_page;

    if (!oled_driver_busy || oled_driver_mode != OLED_DRIVER_MODE_DMA) {
        return;
    }

    /* 最后一个字节写入 DR 后 SPI 可能仍在移位，必须等空闲再放开片选。 */
    SPI_I2S_DMACmd(OLED_SPI, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(OLED_DMA_STREAM, DISABLE);
    oled_spi_wait_idle();
    OLED_CS_SET();

    if (!oled_find_next_page((uint8_t)(oled_driver_page + 1U), &next_page)) {
        oled_finish_async(OLED_OK);
        return;
    }
    oled_start_page_async(next_page);
}

void OLED_DriverHandleError(void)
{
    if (!oled_driver_busy || oled_driver_mode != OLED_DRIVER_MODE_DMA) {
        return;
    }
    SPI_I2S_DMACmd(OLED_SPI, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(OLED_DMA_STREAM, DISABLE);
    DMA_ClearFlag(OLED_DMA_STREAM, OLED_DMA_FLAGS);
    OLED_CS_SET();
    oled_finish_async(OLED_ERROR);
}
