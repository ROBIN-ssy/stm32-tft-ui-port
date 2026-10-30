#ifndef __LCD_INIT_H
#define __LCD_INIT_H

#include "sys.h"

#define USE_HORIZONTAL 0  //���ú�������������ʾ 0��1Ϊ���� 2��3Ϊ����


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W 240
#define LCD_H 320

#else
#define LCD_W 320
#define LCD_H 240
#endif



//-----------------LCD�˿ڶ���----------------

#define LCD_MOSI_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_15)//SDI=MOSI
#define LCD_MOSI_Set() GPIO_SetBits(GPIOB,GPIO_Pin_15)

#define LCD_SCLK_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_13)//SCL=CLK
#define LCD_SCLK_Set() GPIO_SetBits(GPIOB,GPIO_Pin_13)

#define LCD_CS_Clr()   GPIO_ResetBits(GPIOB,GPIO_Pin_12)//CS
#define LCD_CS_Set()   GPIO_SetBits(GPIOB,GPIO_Pin_12)

#define LCD_DC_Clr()   GPIO_ResetBits(GPIOC,GPIO_Pin_5)//DC
#define LCD_DC_Set()   GPIO_SetBits(GPIOC,GPIO_Pin_5)

#define LCD_BLK_Clr()  GPIO_ResetBits(GPIOB,GPIO_Pin_1)//BLK
#define LCD_BLK_Set()  GPIO_SetBits(GPIOB,GPIO_Pin_1)

#define ZK_MISO        GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)//MISO=SDO  ��ȡ�ֿ���������

#define ZK_CS_Set()    GPIO_ResetBits(GPIOB,GPIO_Pin_12)//�ֿ��SPI������LCD��SPI���Ÿ��ã���CSΪ�͵�ƽʱ��ѡ��LCD,��CSΪ�ߵ�ƽʱ��ѡ���ֿ⡣
#define ZK_CS_Clr()    GPIO_SetBits(GPIOB,GPIO_Pin_12)





void LCD_GPIO_Init(void);//��ʼ��GPIO
void LCD_Writ_Bus(u8 dat);//ģ��SPIʱ��
void LCD_WR_DATA8(u8 dat);//д��һ���ֽ�
void LCD_WR_DATA(u16 dat);//д�������ֽ�
void LCD_WR_REG(u8 dat);
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2);
void LCD_Init(void);
void LCD_WR_DATA_DMA(u8 *pData, u16 len);
#endif




