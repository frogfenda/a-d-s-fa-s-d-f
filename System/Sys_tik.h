#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f10x.h"

/* * 裸机环境下的 SysTick 精准延时库 (非中断、轮询查标志位)
 * 注意：仅限不跑操作系统 (RTOS) 的裸机环境使用！
 */

void Delay_Init(void);          // 延时初始化 (必须在 main 函数最开头调用一次)
void Delay_us(uint32_t us);     // 微秒级延时
void Delay_ms(uint16_t ms);     // 毫秒级延时

#endif