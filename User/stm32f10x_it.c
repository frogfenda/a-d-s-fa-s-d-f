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
volatile uint32_t g_RunTimeMs = 0; 

// (保留你原有的 HardFault 等其他中断...)

/**
  * @brief  滴答定时器中断 (系统的心脏，每 1ms 跳动一次)
  */
void SysTick_Handler(void)
{
    g_RunTimeMs++; // 工业级时基产生
}

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f10x_xx.s).                                            */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @}
  */ 


#include "stm32f10x_it.h"
#include "usart.h" // 必须包含，因为我们要用黑板变量

/**
  * @brief  USART1 中断服务函数
  */
#include "stm32f10x_it.h"
#include "usart.h" 

// (保留你原有的 HardFault 等中断...)

/**
  * @brief  USART2 硬件中断服务函数 (专门监听 ESP8266 是否闭嘴)
  */
void USART2_IRQHandler(void)
{
    // 检查 IDLE (空闲) 标志位
    if(USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        // 硬件时序要求：读 SR 后读 DR 清除 IDLE 标志
        volatile uint32_t temp = USART2->SR;
        temp = USART2->DR;
        (void)temp; // 防止编译器警告

        // 暂停 DMA，准备更新指针
        DMA_Cmd(DMA1_Channel6, DISABLE); 

        // 计算本次接收的数据长度
        UART2_RX_Len = RX_MAX_LEN - DMA_GetCurrDataCounter(DMA1_Channel6);
        UART2_RX_Flag = 1; // 竖起旗帜，通知主循环收菜

        // 重置 DMA 接收数量并重启
        DMA_SetCurrDataCounter(DMA1_Channel6, RX_MAX_LEN); 
        DMA_Cmd(DMA1_Channel6, ENABLE); 
    }
}