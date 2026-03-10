#include "main.h"
#include "my_iic.h"
// ==========================================
// 1. 引脚操作宏定义 (极速翻转)
// 假设使用 PB6(SCL) 和 PB7(SDA)
// ==========================================
#define IIC_SCL_1  GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define IIC_SCL_0  GPIO_ResetBits(GPIOB, GPIO_Pin_8)

#define IIC_SDA_1  GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define IIC_SDA_0  GPIO_ResetBits(GPIOB, GPIO_Pin_9)

// 读取 SDA 线的电平状态
#define IIC_SDA_READ()  GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_9)

// 假设你有一个精准的微秒延时函数 (通常用滴答定时器 SysTick 实现)
extern void Delay_us(uint32_t us); 

// ==========================================
// 2. I2C 端口初始化
// ==========================================
void IIC_Init(void)
{					     
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD; // 核心：通用开漏输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
   
    // 初始化空闲状态：SCL 和 SDA 全部释放 (拉高)
    IIC_SCL_1;
    IIC_SDA_1;
}

// ==========================================
// 3. I2C 核心协议波形拼装
// ==========================================

// 产生起始信号 (START)
void IIC_Start(void)
{
    IIC_SDA_1;	  
    IIC_SCL_1;
    Delay_us(4);
    IIC_SDA_0; // SCL为高时，SDA由高变低
    Delay_us(4);
    IIC_SCL_0; // 钳住I2C总线，准备发送或接收数据 
}

// 产生停止信号 (STOP)
void IIC_Stop(void)
{
    IIC_SCL_0;
    IIC_SDA_0; // SCL为低时，准备好SDA的低电平
    Delay_us(4);
    IIC_SCL_1; 
    Delay_us(4);
    IIC_SDA_1; // SCL为高时，SDA由低变高 (发送STOP)
    Delay_us(4);							   	
}

// 等待应答信号 (ACK)
// 返回值：0(接收ACK成功), 1(接收NACK/超时失败)
uint8_t IIC_Wait_Ack(void)
{
    uint8_t ucErrTime = 0;
    IIC_SCL_0;
    IIC_SDA_1; // 主机释放SDA线，把话语权交给从机
    Delay_us(2);	   
    IIC_SCL_1; // 主机拉高时钟，准备读取
    Delay_us(2);
    
    // 死盯 SDA 线，看从机有没有拉低
    while(IIC_SDA_READ() == 1) 
    {
        ucErrTime++;
        if(ucErrTime > 250) // 超时保护机制！绝不死锁！
        {
            IIC_Stop();
            return 1; // 报错返回
        }
    }
    IIC_SCL_0; // 收到ACK，拉低时钟结束该位
    return 0;  
} 

// 主机主动产生 ACK 应答 (勾引从机继续发)
void IIC_Ack(void)
{
    IIC_SCL_0;
    IIC_SDA_0; // SDA拉低表示 ACK
    Delay_us(2);
    IIC_SCL_1;
    Delay_us(2);
    IIC_SCL_0;
}

// 主机主动产生 NACK 非应答 (强行切断通信)
void IIC_NAck(void)
{
    IIC_SCL_0;
    IIC_SDA_1; // SDA拉高表示 NACK
    Delay_us(2);
    IIC_SCL_1;
    Delay_us(2);
    IIC_SCL_0;
}

// 发送一个字节 (从高位到低位)
void IIC_Send_Byte(uint8_t txd)
{                        
    uint8_t t;   
    IIC_SCL_0; // 拉低时钟开始数据传输
    for(t = 0; t < 8; t++)
    {              
        if((txd & 0x80) >> 7) {
            IIC_SDA_1;
        } else {
            IIC_SDA_0;
        }
        txd <<= 1; 	  
        Delay_us(2);
        IIC_SCL_1; // 拉高时钟，让从机去读
        Delay_us(2); 
        IIC_SCL_0; // 拉低时钟，准备下一位
        Delay_us(2);
    }	 
}

// 读取一个字节
// 参数 ack: 1代表读完后给从机发ACK，0代表读完后发NACK
uint8_t IIC_Read_Byte(unsigned char ack)
{
    uint8_t i, receive = 0;
    IIC_SDA_1; // 释放SDA，准备读取
    
    for(i = 0; i < 8; i++)
    {
        IIC_SCL_0; 
        Delay_us(2);
        IIC_SCL_1; // 拉高时钟，此时SDA上的数据是有效的
        Delay_us(2);
        
        receive <<= 1; // 左移腾出空位
        if(IIC_SDA_READ()) {
            receive++; // 如果读到高电平，最低位置1
        }
        Delay_us(1); 
    }					 
    
    // 发送应答
    if (ack == 1) {
        IIC_Ack();
    } else {
        IIC_NAck();
    }
    return receive;
}