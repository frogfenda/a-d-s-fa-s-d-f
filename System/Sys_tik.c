#include "main.h"
#include "Sys_tik.h"


// 静态全局变量，用来保存乘数，避免每次延时都去做除法运算（极度压榨 CPU 算力）
static uint8_t  fac_us = 0; // 1微秒需要的滴答数
static uint16_t fac_ms = 0; // 1毫秒需要的滴答数

/**
 * @brief  初始化延迟函数
 * @note   配置 SysTick 时钟源为 HCLK/8。
 * 假设主频 HCLK = 72MHz，那么 SysTick 的频率就是 9MHz。
 * 意思是：1秒钟跳 9,000,000 下。
 */
void Delay_Init(void)
{
    // 1. 选择 SysTick 时钟源为 HCLK/8 = 9MHz
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
    
    // 2. 计算出 1 微秒需要跳多少下：9000000 / 1000000 = 9 下
    // SystemCoreClock 在库中默认是 72000000
    fac_us = SystemCoreClock / 8000000; 
    
    // 3. 计算出 1 毫秒需要跳多少下：9 * 1000 = 9000 下
    fac_ms = (uint16_t)fac_us * 1000;   
}

/**
 * @brief  微秒级精准延时
 * @param  us: 要延时的微秒数。
 * 注意：us * fac_us 的值不能超过 24 位寄存器的最大值 (16777215)
 * 所以最大延时 us <= 16777215 / 9 ≈ 1,864,135 微秒
 */
void Delay_us(uint32_t us)
{
    uint32_t temp;
    
    SysTick->LOAD = us * fac_us;              // 将计算好的滴答数装入重装载寄存器
    SysTick->VAL  = 0x00;                     // 清空当前计数值，让它从 LOAD 的值开始倒数
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // 开启定时器！(CTRL寄存器位0 置1)
    
    // 死等：不断读取 CTRL 寄存器
    // 条件1：(temp & 0x01) 确保定时器还在使能状态
    // 条件2：!(temp & (1<<16)) 确保 COUNTFLAG 标志位还没变成 1 (变成 1 就说明倒数到 0 了)
    do {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1 << 16)));
    
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // 延时结束，立刻关闭定时器！
    SysTick->VAL  = 0x00;                      // 清空计数值，深藏功与名
}

/**
 * @brief  毫秒级精准延时
 * @param  ms: 要延时的毫秒数。
 * 最大延时 ms <= 16777215 / 9000 ≈ 1864 毫秒
 */
void Delay_ms(uint16_t ms)
{
    uint32_t temp;
    
    SysTick->LOAD = (uint32_t)ms * fac_ms;    // 装入毫秒的滴答数
    SysTick->VAL  = 0x00;                     // 清空当前计数值
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // 开启定时器！
    
    do {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1 << 16)));
    
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // 延时结束，关闭定时器
    SysTick->VAL  = 0x00;                      // 清空计数值
}