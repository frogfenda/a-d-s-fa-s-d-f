#include "Board_init.h"
#include "Sys_tik.h"
#include "usart.h"
#include "shtc3.h"

void Board_Init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    SysTick_Init_For_Gateway(); // 🚨 心跳起搏器：没有它系统必死！
    
    UART1_Init(115200); // 调试串口
    UART2_Init(115200); // ESP8266 串口
    
    SHTC3_Init();       // 传感器初始化
}