#ifndef __BOARD_INIT_H
#define __BOARD_INIT_H

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

// ==========================================================
// 💡 架构师级头文件管理：在这里引入所有底层外设的头文件
// ==========================================================
#include "Sys_tik.h"    // 滴答定时器
#include "usart.h"      // 串口与 DMA 通信模块
#include "shtc3.h"      // 温湿度传感器 (注意你的头文件里函数名叫 SHT30)
#include "w25q64.h"     // SPI Flash 存储器

// 统一采用大驼峰命名法
void Board_Init(void);

#endif