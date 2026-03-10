#ifndef  __my_iic
#define __my_iic
void IIC_Init(void);
 void IIC_Start(void);
 void IIC_Stop(void);
 uint8_t IIC_Wait_Ack(void);
void IIC_Send_Byte(uint8_t txd);
uint8_t IIC_Read_Byte(unsigned char ack);
uint8_t SHT30_Get_Temp_Humi(float *Temperature, float *Humidity);
#endif 

