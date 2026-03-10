#ifndef __SHTC3_H
#define __SHTC3_H

#include "stm32f10x.h"
#include <stdbool.h>

#define SHTC3_I2C_ADDR_WRITE  0xE0
#define SHTC3_I2C_ADDR_READ   0xE1

void SHTC3_Init(void);
bool SHTC3_Get_Temp_Humi(float *temp, float *humi);

#endif