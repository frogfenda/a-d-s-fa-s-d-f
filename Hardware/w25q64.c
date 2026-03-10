#include "w25q64.h"

// ==========================================
// 内部底层函数：硬件 SPI 交换一个字节
// ==========================================
uint8_t SPI_Hardware_SwapByte(uint8_t txData)
{
    // 1. 等待发送缓冲区空 (TXE)
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    // 2. 发送数据 (此时硬件自动产生时钟和移位)
    SPI_I2S_SendData(SPI1, txData);
    // 3. 等待接收缓冲区非空 (RXNE)，说明 8 个时钟打完了，数据挤回来了
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    // 4. 返回收到的数据
    return SPI_I2S_ReceiveData(SPI1);
}

// ==========================================
// 1. W25Q64 整体初始化 (包含引脚与 SPI 引擎)
// ==========================================
void W25Q64_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA|RCC_APB2Periph_SPI1,ENABLE);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5|GPIO_Pin_7;
    GPIO_Init(GPIOA,&GPIO_InitStructure);
    // PA6(MISO) -> 浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    // PA4(CS 片选) -> 普通推挽输出 (我们自己用代码控制)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    W25Q64_CS_1;//默认不选择
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4 ;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CRCPolynomial = 7;//默认值
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;//大头还是小头
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_Init (SPI1,&SPI_InitStructure);
    SPI_Cmd (SPI1,ENABLE);

}

// ==========================================
// 内部底层函数：写使能 (解锁)
// ==========================================
void W25Q64_WriteEnable(void)
{
    W25Q64_CS_0;
    SPI_Hardware_SwapByte(0x06); // 写使能指令
    W25Q64_CS_1; 
}

// ==========================================
// 内部底层函数：死等完成 (判忙)
// ==========================================
void W25Q64_WaitBusy(void)
{
    uint8_t status;
    W25Q64_CS_0;
    SPI_Hardware_SwapByte(0x05); // 读状态寄存器指令
    do {
        status = SPI_Hardware_SwapByte(0xFF);
    } while((status & 0x01) == 0x01); // 只要最低位是1，就死等
    W25Q64_CS_1;
}

// ==========================================
// 2. 读取芯片 ID (厂商 0xEF，设备 0x16)
// ==========================================
void W25Q64_ReadID(uint8_t *MID, uint16_t *DID)
{
    W25Q64_CS_0; 
    SPI_Hardware_SwapByte(0x90); 
    SPI_Hardware_SwapByte(0x00);
    SPI_Hardware_SwapByte(0x00);
    SPI_Hardware_SwapByte(0x00);
    *MID = SPI_Hardware_SwapByte(0xFF); 
    *DID = SPI_Hardware_SwapByte(0xFF); 
    W25Q64_CS_1; 
}

// ==========================================
// 3. 扇区擦除 (极其暴力，4096字节清空为 0xFF)
// ==========================================
void W25Q64_SectorErase(uint32_t SectorAddress)
{
    W25Q64_WaitBusy(); // 擦除极其漫长，必须判忙！
    W25Q64_WriteEnable(); // 擦除前必须解锁！
    
    W25Q64_CS_0;
    SPI_Hardware_SwapByte(0x20); // 扇区擦除指令
    SPI_Hardware_SwapByte((SectorAddress >> 16) & 0xFF); 
    SPI_Hardware_SwapByte((SectorAddress >> 8)  & 0xFF);
    SPI_Hardware_SwapByte(SectorAddress & 0xFF);
    W25Q64_CS_1; 
    
    
}

// ==========================================
// 4. 页编程 (写入数据，1 把变成 0)
// ==========================================
void W25Q64_PageProgram(uint32_t Address, uint8_t *DataArray, uint16_t Count)
{
    W25Q64_WaitBusy(); // 等待烧录完成
    uint16_t i;
    W25Q64_WriteEnable(); // 写数据前必须解锁！
    
    W25Q64_CS_0;
    SPI_Hardware_SwapByte(0x02); // 页编程指令
    SPI_Hardware_SwapByte((Address >> 16) & 0xFF);
    SPI_Hardware_SwapByte((Address >> 8)  & 0xFF);
    SPI_Hardware_SwapByte(Address & 0xFF);
    
    for(i = 0; i < Count; i++) {
        SPI_Hardware_SwapByte(DataArray[i]);
    }
    W25Q64_CS_1; 
    
    
}

// ==========================================
// 5. 任意读取数据
// ==========================================
void W25Q64_ReadData(uint32_t Address, uint8_t *DataArray, uint32_t Count)
{
    uint32_t i;
    W25Q64_WaitBusy(); // 等待烧录完成
    W25Q64_CS_0;

    SPI_Hardware_SwapByte(0x03); // 读数据指令
    SPI_Hardware_SwapByte((Address >> 16) & 0xFF);
    SPI_Hardware_SwapByte((Address >> 8)  & 0xFF);
    SPI_Hardware_SwapByte(Address & 0xFF);
    
    for(i = 0; i < Count; i++) {
        DataArray[i] = SPI_Hardware_SwapByte(0xFF);
    }
    W25Q64_CS_1;
}