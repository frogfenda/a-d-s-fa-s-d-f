#include "shtc3.h"
#include "my_iic.h"
#include <stdio.h>

static uint8_t SHTC3_Calc_CRC(uint16_t data)
{
    uint8_t crc = 0xFF;
    uint8_t crc_data[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    for(uint8_t i = 0; i < 2; i++) {
        crc ^= crc_data[i];
        for(uint8_t bit = 8; bit > 0; --bit) {
            if(crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc = (crc << 1);
        }
    }
    return crc;
}

static bool SHTC3_Write_Command(uint16_t cmd)
{
    IIC_Start();
    IIC_Send_Byte(SHTC3_I2C_ADDR_WRITE);
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    IIC_Send_Byte((uint8_t)(cmd >> 8));   
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    IIC_Send_Byte((uint8_t)(cmd & 0xFF)); 
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    IIC_Stop();
    return true;
}

void SHTC3_Init(void)
{
    IIC_Init();
    SHTC3_Write_Command(0x3517); // Wakeup
    Delay_us(300);
    SHTC3_Write_Command(0xB098); // Sleep
    printf(">> [HW] SHTC3 SCL(PB6)/SDA(PB7) Initialized.\r\n");
}

bool SHTC3_Get_Temp_Humi(float *temp, float *humi)
{
    uint8_t data[6];
    
    if (!SHTC3_Write_Command(0x3517)) return false; // Wakeup
    Delay_us(300);
    
    if (!SHTC3_Write_Command(0x7866)) { // Measure Normal Mode
        SHTC3_Write_Command(0xB098); return false;
    }
    
    Delay_ms(15); // 硬件转换耗时
    
    IIC_Start();
    IIC_Send_Byte(SHTC3_I2C_ADDR_READ);
    if (IIC_Wait_Ack()) { IIC_Stop(); SHTC3_Write_Command(0xB098); return false; }
    
    for(uint8_t i=0; i<5; i++) data[i] = IIC_Read_Byte(1); // 发 ACK
    data[5] = IIC_Read_Byte(0); // 最后一个发 NACK
    IIC_Stop();
    
    SHTC3_Write_Command(0xB098); // 立刻 Sleep 降温
    
    uint16_t raw_t = (data[0] << 8) | data[1];
    uint16_t raw_h = (data[3] << 8) | data[4];
    
    // 💡 架构师防御：数据清洗，只要 CRC 不对，宁可丢包绝不上报假数据
    if (SHTC3_Calc_CRC(raw_t) != data[2] || SHTC3_Calc_CRC(raw_h) != data[5]) return false;
    
    *temp = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    *humi = 100.0f * ((float)raw_h / 65536.0f);
    return true;
}