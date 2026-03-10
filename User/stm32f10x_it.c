/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.6.0
  * @date    20-September-2021
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2011 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "main.h"
/** @addtogroup STM32F10x_StdPeriph_Template
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */


// (保留你原有的 HardFault 等其他中断...)

/**
  * @brief  滴答定时器中断 (系统的心脏，每 1ms 跳动一次)
  */
#include "stm32f10x_it.h"
#include "Sys_tik.h"
#include "usart.h" 

// 引入 Sys_tik.c 中的递减器
extern volatile uint32_t g_DelayTicks_ms;

/**
  * @brief  滴答定时器中断 (系统的心脏，1ms)
  */
void SysTick_Handler(void)
{
    g_RunTimeMs++; // 喂养非阻塞状态机
    
    if(g_DelayTicks_ms > 0) {
        g_DelayTicks_ms--; // 喂养阻塞延时函数
    }
}

/**
  * @brief  USART2 硬件中断服务函数 (监听 ESP8266 突发数据)
  */
void USART2_IRQHandler(void)
{
    // 💡 架构师注：检测到空闲帧，说明一整包数据已经乖乖躺在 SRAM 里了
    if(USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        // 硬件时序强制要求：先读 SR，再读 DR，以清除 IDLE 标志位，否则死锁中断
        volatile uint32_t temp = USART2->SR;
        temp = USART2->DR;
        (void)temp; 

        DMA_Cmd(DMA1_Channel6, DISABLE); // 暂停 DMA 搬运

        // 计算本次实际接收长度：Buffer总长 - DMA剩余待搬运量
        UART2_RX_Len = RX_MAX_LEN - DMA_GetCurrDataCounter(DMA1_Channel6);
        UART2_RX_Flag = 1; // 竖起旗帜，通知主循环

        // 重置 DMA 指针，准备迎接下一包
        DMA_SetCurrDataCounter(DMA1_Channel6, RX_MAX_LEN); 
        DMA_Cmd(DMA1_Channel6, ENABLE); 
    }
}