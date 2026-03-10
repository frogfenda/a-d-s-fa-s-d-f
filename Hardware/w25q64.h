#ifndef __W25Q64_H
#define __W25Q64_H

#include "main.h"

// ==========================================
// 引脚宏定义 (使用 PA4 作为软件片选)
// ==========================================
#define W25Q64_CS_0     GPIO_ResetBits(GPIOA, GPIO_Pin_4)
#define W25Q64_CS_1     GPIO_SetBits(GPIOA, GPIO_Pin_4)

// ==========================================
// 核心操作函数声明
// ==========================================
void W25Q64_Init(void);  // 包含 SPI 硬件初始化
void W25Q64_ReadID(uint8_t *MID, uint16_t *DID);
void W25Q64_SectorErase(uint32_t SectorAddress);
void W25Q64_PageProgram(uint32_t Address, uint8_t *DataArray, uint16_t Count);
void W25Q64_ReadData(uint32_t Address, uint8_t *DataArray, uint32_t Count);

#endif