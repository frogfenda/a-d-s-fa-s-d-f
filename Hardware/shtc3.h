#ifndef __SHTC3_H
#define __SHTC3_H

#include "stm32f10x.h"
#include <stdbool.h>

// 💡 架构师级：SHTC3 物理地址与底层指令宏定义
#define SHTC3_I2C_ADDR_WRITE  0xE0
#define SHTC3_I2C_ADDR_READ   0xE1

#define SHTC3_CMD_WAKEUP      0x3517
#define SHTC3_CMD_SLEEP       0xB098
#define SHTC3_CMD_MEASURE     0x7866  // 正常模式，温度优先，禁用 Clock Stretching
#define SHTC3_CMD_READ_ID     0xEFC8

void SHTC3_Init(void);
bool SHTC3_Read_ID(uint16_t *id);
bool SHTC3_Get_Temp_Humi(float *temp, float *humi);

#endif