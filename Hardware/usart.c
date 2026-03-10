#include "usart.h"

// 实体化专线黑板变量
uint8_t  UART2_RX_Buffer[RX_MAX_LEN]; 
volatile uint8_t  UART2_RX_Flag = 0;  
volatile uint16_t UART2_RX_Len = 0;   

// ==============================================================
// 听诊器专区：保留给 USART1 的 printf 重定向
// ==============================================================
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/**
 * @brief  初始化 USART1 (听诊器，仅使用常规模式，引脚 PA9/PA10)
 */
void UART1_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    // 开启 USART1 和 GPIOA 时钟 (都在高速总线 APB2 上)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    // PA9(TX) 推挽复用, PA10(RX) 浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  初始化 USART2 (加密专线，挂载 DMA 与 IDLE，引脚 PA2/PA3)
 */
void UART2_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    // 🚨 架构师避坑：USART2 挂在慢速总线 APB1 上！必须分开开启！
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); 

    // PA2(TX) 推挽复用, PA3(RX) 浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    // 配置 NVIC：给 USART2 开通 VIP 报警通道
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // ==========================================================
    // 🚨 查表得知：USART2_RX 被焊死在 DMA1_Channel6
    // ==========================================================
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR; 
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)UART2_RX_Buffer; 
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                
    DMA_InitStructure.DMA_BufferSize = RX_MAX_LEN;                    
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;           
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; 
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                     
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;               
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE); 

    // ==========================================================
    // 🚨 查表得知：USART2_TX 被焊死在 DMA1_Channel7
    // ==========================================================
    DMA_DeInit(DMA1_Channel7);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR; 
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)0; 
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;                
    DMA_InitStructure.DMA_BufferSize = 0;               
    DMA_Init(DMA1_Channel7, &DMA_InitStructure);

    // 开启 USART2 的 IDLE 空闲报警器和 DMA 双向物理触发线
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);                   
    USART_DMACmd(USART2, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE); 
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief  全自动一键发射函数 (专供 USART2 / DMA1_Channel7 使用)
 */
void UART2_DMA_Send(uint8_t *data, uint16_t len)
{
    DMA_Cmd(DMA1_Channel7, DISABLE);          
    DMA1_Channel7->CMAR = (uint32_t)data;     
    DMA1_Channel7->CNDTR = len;               
    DMA_Cmd(DMA1_Channel7, ENABLE);           
}