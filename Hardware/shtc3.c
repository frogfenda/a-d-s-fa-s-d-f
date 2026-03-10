#include "stm32f10x.h"
#include "main.h"

#define SHT30_ADDR_WRITE  0x44 << 1      // 0x88
#define SHT30_ADDR_READ  (0x44 << 1) | 1 // 0x89

// ==========================================
// 1. 下发 16 位测量命令
// ==========================================
void SHT30_StartMeasurement(void)
{
    IIC_Start();
    IIC_Send_Byte(SHT30_ADDR_WRITE);
    if(IIC_Wait_Ack()) {
        IIC_Stop(); return; // 寻址失败，防卡死
    }
    
    // 发送高精度测量指令 0x2C06
    IIC_Send_Byte(0x2C);
    IIC_Wait_Ack();
    IIC_Send_Byte(0x06);
    IIC_Wait_Ack();
    
    IIC_Stop();
}

// ==========================================
// 2. 读取 6 字节原始数据
// ==========================================
uint8_t SHT30_ReadRawData(uint8_t *buffer)
{
    uint8_t i;
    
    IIC_Start();
    IIC_Send_Byte(SHT30_ADDR_READ);
    if(IIC_Wait_Ack()) {
        IIC_Stop(); return 1; // 寻址失败
    }
    
    // 连续读取6个字节
    for(i = 0; i < 6; i++) {
        if(i == 5) {
            buffer[i] = IIC_Read_Byte(0); // 最后一个回 NACK
        } else {
            buffer[i] = IIC_Read_Byte(1); // 前5个回 ACK
        }
    }
    
    IIC_Stop();
    return 0; // 成功
}

// ==========================================
// 3. 顶层业务：获取物理温湿度
// ==========================================
// 架构师附赠：极其硬核的 CRC8 校验算法 (完全匹配SHT30手册)
uint8_t SHT30_CheckCRC(uint8_t *data, uint8_t num_bytes, uint8_t checksum)
{
    uint8_t crc = 0xFF; // 初始值
    uint8_t i, j;
    
    for(i = 0; i < num_bytes; i++) {
        crc ^= data[i];
        for(j = 8; j > 0; --j) {
            if(crc & 0x80) crc = (crc << 1) ^ 0x31; // 多项式 0x31
            else           crc = (crc << 1);
        }
    }
    if(crc != checksum) return 1; // 校验失败
    return 0; // 校验成功
}

// 最终暴露给 main 函数调用的终极接口
// 返回值：0代表数据完美，1代表传感器掉线或数据损坏
uint8_t SHT30_Get_Temp_Humi(float *Temperature, float *Humidity)
{
    uint8_t rx_buf[6];
    uint16_t raw_temp, raw_humi;
    
    // 1. 发起测量
    SHT30_StartMeasurement();
    
    // 2. 物理世界需要时间，SHT30高精度需要15ms，这里给20ms裕量
    Delay_ms(20); 
    
    // 3. 读回数据
    if(SHT30_ReadRawData(rx_buf) == 0) 
    {
        // 4. 数据安全校验 (工业级代码的尊严：绝对不采信乱码)
        // 校验温度的两个字节
        if(SHT30_CheckCRC(&rx_buf[0], 2, rx_buf[2]) != 0) return 1;
        // 校验湿度的两个字节
        if(SHT30_CheckCRC(&rx_buf[3], 2, rx_buf[5]) != 0) return 1;
        
        // 5. 数据缝合
        raw_temp = (rx_buf[0] << 8) | rx_buf[1];
        raw_humi = (rx_buf[3] << 8) | rx_buf[4];
        
        // 6. 浮点数计算换算
        *Temperature = -45.0f + (175.0f * ((float)raw_temp / 65535.0f));
        *Humidity    = 100.0f * ((float)raw_humi / 65535.0f);
        
        return 0; // 一切完美
    }
    
    return 1; // 读取失败
}