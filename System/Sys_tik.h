#ifndef __SYS_TIK_H
#define __SYS_TIK_H

#include "stm32f10x.h"

// 💡 架构师注：暴露全局时间戳，供主循环非阻塞状态机使用
extern volatile uint32_t g_RunTimeMs;

void SysTick_Init_For_Gateway(void);
void Delay_us(uint32_t us);
void Delay_ms(uint32_t ms);

#endif