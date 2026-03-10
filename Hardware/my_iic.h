#ifndef __MY_IIC_H
#define __MY_IIC_H

#include "stm32f10x.h"
#include "Sys_tik.h"

void IIC_Init(void);
void IIC_Start(void);
void IIC_Stop(void);
uint8_t IIC_Wait_Ack(void);
void IIC_Ack(void);
void IIC_NAck(void);
void IIC_Send_Byte(uint8_t txd);
uint8_t IIC_Read_Byte(unsigned char ack);

#endif