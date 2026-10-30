#include "lcd_init.h"
#include "delay.h"
#include "lcd_spi.h"

void LCD_GPIO_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB|RCC_AHB1Periph_GPIOC, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

void LCD_Writ_Bus(u8 dat)
{
	LCD_CS_Clr();
	LCD_SPI_WriteByte(dat);
	LCD_CS_Set();
}

void LCD_WR_DATA8(u8 dat)
{
	LCD_CS_Clr();
	LCD_SPI_WriteByte(dat);
	LCD_CS_Set();
}

void LCD_WR_DATA(u16 dat)
{
	LCD_CS_Clr();
	LCD_SPI_WriteByte(dat >> 8);
	LCD_SPI_WriteByte(dat);
	LCD_CS_Set();
}

void LCD_WR_REG(u8 dat)
{
	LCD_DC_Clr();
	LCD_CS_Clr();
	LCD_SPI_WriteByte(dat);
	LCD_CS_Set();
	LCD_DC_Set();
}

void LCD_WR_DATA_DMA(u8 *pData, u16 len)
{
	LCD_CS_Clr();
	LCD_SPI_WriteData_DMA(pData, len);
	LCD_CS_Set();
}


/******************************************************************************
      ����˵����������ʼ�ͽ�����ַ
      ������ݣ�x1,x2 �����е���ʼ�ͽ�����ַ
                y1,y2 �����е���ʼ�ͽ�����ַ
      ����ֵ��  ��
******************************************************************************/
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2)
{
	if(USE_HORIZONTAL == 0)
	{
		LCD_WR_REG(0x2a);//�е�ַ����
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//�е�ַ����
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//������д
	}
	else if(USE_HORIZONTAL == 1)
	{
		LCD_WR_REG(0x2a);//�е�ַ����
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//�е�ַ����
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//������д
	}
	else if(USE_HORIZONTAL == 2)
	{
		LCD_WR_REG(0x2a);//�е�ַ����
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//�е�ַ����
		LCD_WR_DATA(y2);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//������д
	}
	else
	{
		LCD_WR_REG(0x2a);//�е�ַ����
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2 );
		LCD_WR_REG(0x2b);//�е�ַ����
		LCD_WR_DATA(y1 );
		LCD_WR_DATA(y2 );
		LCD_WR_REG(0x2c);//������д
	}
}

void LCD_Init(void)
{
	LCD_GPIO_Init();
	LCD_SPI_Init();

	LCD_BLK_Set();
	delay_ms(100);

	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out
	delay_ms(120);              //Delay 120ms
	LCD_WR_REG(0xCF);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0xC1);
    LCD_WR_DATA8(0X30);
    LCD_WR_REG(0xED);
    LCD_WR_DATA8(0x64);
    LCD_WR_DATA8(0x03);
    LCD_WR_DATA8(0X12);
    LCD_WR_DATA8(0X81);
    LCD_WR_REG(0xE8);
    LCD_WR_DATA8(0x85);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x79);
    LCD_WR_REG(0xCB);
    LCD_WR_DATA8(0x39);
    LCD_WR_DATA8(0x2C);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x34);
    LCD_WR_DATA8(0x02);
    LCD_WR_REG(0xF7);
    LCD_WR_DATA8(0x20);
    LCD_WR_REG(0xEA);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x00);
    LCD_WR_REG(0xC0); //Power control
    LCD_WR_DATA8(0x1D); //VRH[5:0]
    LCD_WR_REG(0xC1); //Power control
    LCD_WR_DATA8(0x12); //SAP[2:0];BT[3:0]
    LCD_WR_REG(0xC5); //VCM control
    LCD_WR_DATA8(0x33);
    LCD_WR_DATA8(0x3F);
    LCD_WR_REG(0xC7); //VCM control
    LCD_WR_DATA8(0x92);
    LCD_WR_REG(0x3A); // Memory Access Control
    LCD_WR_DATA8(0x55);
    LCD_WR_REG(0x36); // Memory Access Control
    if (USE_HORIZONTAL == 0)LCD_WR_DATA8(0x08);
    else if (USE_HORIZONTAL == 1)LCD_WR_DATA8(0xC8);
    else if (USE_HORIZONTAL == 2)LCD_WR_DATA8(0x78);
    else LCD_WR_DATA8(0xA8);
    LCD_WR_REG(0xB1);
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x12);
    LCD_WR_REG(0xB6); // Display Function Control
    LCD_WR_DATA8(0x0A);
    LCD_WR_DATA8(0xA2);

    LCD_WR_REG(0x44);
    LCD_WR_DATA8(0x02);

    LCD_WR_REG(0xF2); // 3Gamma Function Disable
    LCD_WR_DATA8(0x00);
    LCD_WR_REG(0x26); //Gamma curve selected
    LCD_WR_DATA8(0x01);
    LCD_WR_REG(0xE0); //Set Gamma
    LCD_WR_DATA8(0x0F);
    LCD_WR_DATA8(0x22);
    LCD_WR_DATA8(0x1C);
    LCD_WR_DATA8(0x1B);
    LCD_WR_DATA8(0x08);
    LCD_WR_DATA8(0x0F);
    LCD_WR_DATA8(0x48);
    LCD_WR_DATA8(0xB8);
    LCD_WR_DATA8(0x34);
    LCD_WR_DATA8(0x05);
    LCD_WR_DATA8(0x0C);
    LCD_WR_DATA8(0x09);
    LCD_WR_DATA8(0x0F);
    LCD_WR_DATA8(0x07);
    LCD_WR_DATA8(0x00);
    LCD_WR_REG(0XE1); //Set Gamma
    LCD_WR_DATA8(0x00);
    LCD_WR_DATA8(0x23);
    LCD_WR_DATA8(0x24);
    LCD_WR_DATA8(0x07);
    LCD_WR_DATA8(0x10);
    LCD_WR_DATA8(0x07);
    LCD_WR_DATA8(0x38);
    LCD_WR_DATA8(0x47);
    LCD_WR_DATA8(0x4B);
    LCD_WR_DATA8(0x0A);
    LCD_WR_DATA8(0x13);
    LCD_WR_DATA8(0x06);
    LCD_WR_DATA8(0x30);
    LCD_WR_DATA8(0x38);
    LCD_WR_DATA8(0x0F);
    LCD_WR_REG(0x29); //Display on
}








