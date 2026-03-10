#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include <stdio.h>
#include <stdbool.h> // 引入布尔类型

#define RX_MAX_LEN 256  // 建议给足一点缓存，ESP8266回传有时很长

extern uint8_t  UART2_RX_Buffer[RX_MAX_LEN]; 
extern volatile uint8_t  UART2_RX_Flag;  
extern volatile uint16_t UART2_RX_Len;   

void UART1_Init(uint32_t bound);
void UART2_Init(uint32_t bound);

// 💡 架构师级：将无脑发送改为带状态校验的安全发送
bool UART2_DMA_Send_Safe(uint8_t *data, uint16_t len);

#endif