#include "Board_init.h"

void Board_Init(void)
{
    SystemInit();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    Delay_Init();

    // ===============================================
    // 🚨 架构师双轨制启动：两条串口独立工作！
    // ===============================================
    UART1_Init(115200); // 启动听诊器 (连电脑)
    UART2_Init(115200); // 启动加密专线 (连 ESP8266)

    W25Q64_Init(); 
     IIC_Init(); // 如果有就保留
}