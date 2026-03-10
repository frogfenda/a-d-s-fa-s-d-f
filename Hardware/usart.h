#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include <stdio.h>

#define RX_MAX_LEN 256  

// ==========================================================
// 【专线通信黑板】 专门给 USART2 (ESP8266) 使用
// ==========================================================
extern uint8_t  UART2_RX_Buffer[RX_MAX_LEN]; 
extern volatile uint8_t  UART2_RX_Flag;      
extern volatile uint16_t UART2_RX_Len;       

// 函数声明
void UART1_Init(uint32_t bound); // 听诊器：负责 printf
void UART2_Init(uint32_t bound); // 专线：负责 ESP8266 + DMA
void UART2_DMA_Send(uint8_t *data, uint16_t len); // 专线发射按钮

int fputc(int ch, FILE *f);

#endif