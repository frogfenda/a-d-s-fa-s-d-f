#include "my_iic.h"

// 💡 架构师注：严格映射到 PB6 (SCL) 和 PB7 (SDA)
#define IIC_SCL_1  GPIO_SetBits(GPIOB, GPIO_Pin_6)
#define IIC_SCL_0  GPIO_ResetBits(GPIOB, GPIO_Pin_6)
#define IIC_SDA_1  GPIO_SetBits(GPIOB, GPIO_Pin_7)
#define IIC_SDA_0  GPIO_ResetBits(GPIOB, GPIO_Pin_7)
#define IIC_READ_SDA  GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7)

void IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // 🚨 架构师防御：必须是 GPIO_Mode_Out_OD (开漏输出)，否则发 ACK 时会烧管脚！
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    IIC_SCL_1;
    IIC_SDA_1;
}

void IIC_Start(void)
{
    IIC_SDA_1; IIC_SCL_1; Delay_us(4);
    IIC_SDA_0; Delay_us(4);
    IIC_SCL_0; 
}

void IIC_Stop(void)
{
    IIC_SCL_0; IIC_SDA_0; Delay_us(4);
    IIC_SCL_1; Delay_us(4);
    IIC_SDA_1; Delay_us(4);
}

uint8_t IIC_Wait_Ack(void)
{
    uint8_t ucErrTime = 0;
    IIC_SDA_1; Delay_us(1); // 释放 SDA 给从机
    IIC_SCL_1; Delay_us(1);
    while(IIC_READ_SDA) {
        ucErrTime++;
        if(ucErrTime > 250) {
            IIC_Stop(); return 1; // 超时 NACK
        }
    }
    IIC_SCL_0; 
    return 0; // ACK 成功
}

void IIC_Ack(void)
{
    IIC_SCL_0; IIC_SDA_0; Delay_us(2);
    IIC_SCL_1; Delay_us(2);
    IIC_SCL_0;
}

void IIC_NAck(void)
{
    IIC_SCL_0; IIC_SDA_1; Delay_us(2);
    IIC_SCL_1; Delay_us(2);
    IIC_SCL_0;
}

void IIC_Send_Byte(uint8_t txd)
{                        
    uint8_t t;   
    IIC_SCL_0;
    for(t=0; t<8; t++) {              
        if((txd & 0x80) >> 7) IIC_SDA_1;
        else IIC_SDA_0;
        txd <<= 1;       
        Delay_us(2);   
        IIC_SCL_1; Delay_us(2); 
        IIC_SCL_0; Delay_us(2);
    }    
}

uint8_t IIC_Read_Byte(unsigned char ack)
{
    uint8_t i, receive = 0;
    IIC_SDA_1; // 主机释放总线
    for(i=0; i<8; i++) {
        IIC_SCL_0; Delay_us(2);
        IIC_SCL_1; Delay_us(2);
        receive <<= 1;
        if(IIC_READ_SDA) receive++;   
    }                     
    if(!ack) IIC_NAck();
    else IIC_Ack();
    return receive;
}