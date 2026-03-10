#include "shtc3.h"
#include "my_iic.h" // 引入你的软件 I2C 引擎
#include "main.h"   // 假设你的 Delay 函数在这里声明，如果没有请自行补充 extern

// 声明外部延时函数 (根据你的工程实际情况修改)

/**
 * @brief  CRC-8 校验引擎 (多项式 0x31，初始值 0xFF)
 * @param  data: 传感器传来的 16 位温/湿度原始数据
 * @return 算出的 8 位 CRC 校验码，用于和传感器发来的第三个字节比对
 */
static uint8_t SHTC3_Calc_CRC(uint16_t data)
{
    uint8_t crc = 0xFF;
    uint8_t crc_data[2];
    uint8_t i, bit;
    
    crc_data[0] = (uint8_t)(data >> 8);   // MSB
    crc_data[1] = (uint8_t)(data & 0xFF); // LSB

    for(i = 0; i < 2; i++) 
    {
        crc ^= crc_data[i];
        for(bit = 8; bit > 0; --bit) 
        {
            if(crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief 内部函数：发送 16 位指令
 */
static bool SHTC3_Write_Command(uint16_t cmd)
{
    IIC_Start();
    IIC_Send_Byte(SHTC3_I2C_ADDR_WRITE);
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; } // NACK 防死锁
    
    IIC_Send_Byte((uint8_t)(cmd >> 8));   // 发送高 8 位指令
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    
    IIC_Send_Byte((uint8_t)(cmd & 0xFF)); // 发送低 8 位指令
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    
    IIC_Stop();
    return true;
}

/**
 * @brief SHTC3 唤醒状态机 (强制要求)
 */
static bool SHTC3_Wakeup(void)
{
    bool res = SHTC3_Write_Command(SHTC3_CMD_WAKEUP);
    Delay_us(300); // 🚨 硬件要求：唤醒后必须等待 240us 以上才能接收下一条指令
    return res;
}

/**
 * @brief SHTC3 休眠状态机 (防发热与低功耗)
 */
static bool SHTC3_Sleep(void)
{
    return SHTC3_Write_Command(SHTC3_CMD_SLEEP);
}

/**
 * @brief  初始化 (探测芯片是否存在)
 */
void SHTC3_Init(void)
{
    uint16_t id = 0;
    IIC_Init(); // 初始化你的软件 I2C GPIO
    
    SHTC3_Wakeup();
    if (SHTC3_Read_ID(&id)) {
        printf(">> [HW] SHTC3 Sensor Found! ID: 0x%04X\r\n", id);
    } else {
        printf(">> [ERROR] SHTC3 Sensor Missing or I2C Dead!\r\n");
    }
    SHTC3_Sleep();
}

/**
 * @brief  读取芯片 ID
 */
bool SHTC3_Read_ID(uint16_t *id)
{
    uint8_t id_msb, id_lsb;
    
    if(!SHTC3_Write_Command(SHTC3_CMD_READ_ID)) return false;
    
    IIC_Start();
    IIC_Send_Byte(SHTC3_I2C_ADDR_READ);
    if(IIC_Wait_Ack()) { IIC_Stop(); return false; }
    
    id_msb = IIC_Read_Byte(1); // 读 MSB，发 ACK
    id_lsb = IIC_Read_Byte(0); // 读 LSB，发 NACK (不校验 CRC 了)
    IIC_Stop();
    
    *id = (id_msb << 8) | id_lsb;
    
    // 官方手册：ID 寄存器的 bit[5:0] 必须是 0b000111 (0x07)
    if ((*id & 0x003F) != 0x0007) return false; 
    
    return true;
}

/**
 * @brief  🚀 核心驱动：获取温湿度，带严格 CRC 校验
 * @return true(采集成功并且数据干净) / false(总线错误或校验失败)
 */
bool SHTC3_Get_Temp_Humi(float *temp, float *humi)
{
    uint8_t data[6];
    uint16_t raw_t, raw_h;
    
    // 1. 唤醒传感器
    if (!SHTC3_Wakeup()) return false;
    
    // 2. 发送测量指令
    if (!SHTC3_Write_Command(SHTC3_CMD_MEASURE)) {
        SHTC3_Sleep(); // 失败也要记得让它睡
        return false;
    }
    
    // 3. 🚨 挂起等待测量完成 (硬件需要约 12.1 毫秒)
    // 架构师注：在严格的非阻塞 OS 中这里不该用 delay，但目前我们容忍这 15ms 的小顿挫
    Delay_ms(15); 
    
    // 4. 发起读取数据请求
    IIC_Start();
    IIC_Send_Byte(SHTC3_I2C_ADDR_READ);
    if (IIC_Wait_Ack()) {
        IIC_Stop();
        SHTC3_Sleep();
        return false; 
    }
    
    // 5. 暴吸 6 个字节
    data[0] = IIC_Read_Byte(1); // Temp MSB
    data[1] = IIC_Read_Byte(1); // Temp LSB
    data[2] = IIC_Read_Byte(1); // Temp CRC
    data[3] = IIC_Read_Byte(1); // Humi MSB
    data[4] = IIC_Read_Byte(1); // Humi LSB
    data[5] = IIC_Read_Byte(0); // Humi CRC (最后一个发 NACK)
    IIC_Stop();
    
    // 6. 用完立刻踢入休眠模式
    SHTC3_Sleep();
    
    // 7. 🛡️ 数据清洗区：进行 CRC 交叉比对
    raw_t = (data[0] << 8) | data[1];
    raw_h = (data[3] << 8) | data[4];
    
    if (SHTC3_Calc_CRC(raw_t) != data[2]) {
        printf("=> [WARN] SHTC3 Temp CRC Error!\r\n");
        return false; // 脏数据，丢弃！
    }
    
    if (SHTC3_Calc_CRC(raw_h) != data[5]) {
        printf("=> [WARN] SHTC3 Humi CRC Error!\r\n");
        return false; // 脏数据，丢弃！
    }
    
    // 8. 物理量换算 (根据 SHTC3 官方手册公式)
    *temp = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    *humi = 100.0f * ((float)raw_h / 65536.0f);
    
    return true;
}