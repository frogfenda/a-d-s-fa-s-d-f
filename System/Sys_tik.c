#include "Sys_tik.h"

volatile uint32_t g_RunTimeMs = 0;      // 状态机全局时间戳
volatile uint32_t g_DelayTicks_ms = 0;  // 阻塞延时递减器

/**
 * @brief  初始化 SysTick，产生 1ms 中断
 */
void SysTick_Init_For_Gateway(void) 
{
    // 配置 SysTick 每 1ms 触发一次中断 (72MHz 主频)
    SysTick_Config(SystemCoreClock / 1000); 
}

/**
 * @brief  安全的毫秒级阻塞延时 (期间仍能响应高优先级中断)
 */
void Delay_ms(uint32_t ms) 
{
    g_DelayTicks_ms = ms;
    // 💡 架构师注：死等期间 CPU 不会挂死外设，UART DMA 的 IDLE 中断依然能正常抢占
    while(g_DelayTicks_ms != 0) {
        __NOP(); 
    }
}

/**
 * @brief  粗略的微秒级延时 (利用 CPU 空跑，不干扰 SysTick)
 */
void Delay_us(uint32_t us) 
{
    uint32_t i;
    while(us--) {
        i = 10; // 72MHz 下微调魔数，约等于 1us
        while(i--) {
            __NOP(); // 防编译器过度优化
        }
    }
}