#include "stm32f10x.h"
#include "Board_init.h"
#include "Sys_tik.h"
#include "usart.h"
#include "shtc3.h"
#include "w25q64.h"  
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define WIFI_SSID "qingwafenda"
#define WIFI_PWD  "frogfenda"
#define TCP_IP    "192.168.1.101" 
#define TCP_PORT  "8888"          

#define OFFLINE_BASE_ADDR   0x00100000  
#define OFFLINE_MAX_ADDR    0x00200000  
#define RECORD_SIZE         32          

// ==================================================
// 💾 架构师级：离线缓存队列物理模型定义
// ==================================================
#define OFFLINE_BASE_ADDR   0x00100000  // 数据区起始地址 (第 1MB)
#define OFFLINE_MAX_ADDR    0x00200000  // 数据区结束地址 (给 1MB 空间)
#define RECORD_SIZE         32          

// 🛡️ 架构师新增：元数据区 (Metadata) 参数
#define META_BASE_ADDR      0x00000000  // 预留第 0 个扇区存指针
#define META_SECTOR_SIZE    4096        // 扇区大小 4KB
#define META_BLOCK_SIZE     16          // 每条指针记录 16 字节
#define META_MAGIC          0x5AA5AA5A  // 魔数，证明这是一条有效的指针记录

// 内存中的队列指针
uint32_t off_write_addr = OFFLINE_BASE_ADDR; 
uint32_t off_read_addr  = OFFLINE_BASE_ADDR;

// 元数据块结构体 (严格 16 字节)
typedef struct {
    uint32_t w_addr;   // 写指针
    uint32_t r_addr;   // 读指针
    uint32_t magic;    // 魔数标记
    uint32_t xor_chk;  // 异或校验 (w_addr ^ r_addr ^ magic)
} MetaBlock_t;
/**
 * @brief  上电时加载指针 (扫描日志)
 */
void Load_Offline_Pointers(void) 
{
    MetaBlock_t block;
    uint32_t addr = META_BASE_ADDR;
    uint32_t last_valid_addr = 0xFFFFFFFF;

    // 扫描整个 4KB 扇区，每次步进 16 字节
    for (; addr < META_BASE_ADDR + META_SECTOR_SIZE; addr += META_BLOCK_SIZE) 
    {
        W25Q64_ReadData(addr, (uint8_t*)&block, sizeof(MetaBlock_t));
        
        // 遇到 0xFFFFFFFF 说明后面全空了，停止扫描
        if (block.magic == 0xFFFFFFFF) break; 
        
        // 校验这条记录是否完整未撕裂
        if (block.magic == META_MAGIC && 
            block.xor_chk == (block.w_addr ^ block.r_addr ^ block.magic)) 
        {
            last_valid_addr = addr;
            off_write_addr = block.w_addr;
            off_read_addr  = block.r_addr;
        }
    }

    if (last_valid_addr == 0xFFFFFFFF) {
        printf(">> [SYS] No valid pointers found. Formatting queue...\r\n");
        off_write_addr = OFFLINE_BASE_ADDR;
        off_read_addr  = OFFLINE_BASE_ADDR;
    } else {
        printf(">> [SYS] Pointers Restored! Write: 0x%08X, Read: 0x%08X\r\n", 
                off_write_addr, off_read_addr);
    }
}

/**
 * @brief  状态变更时保存指针 (日志追加与磨损均衡)
 */
void Save_Offline_Pointers(void) 
{
    MetaBlock_t block;
    uint32_t addr = META_BASE_ADDR;
    uint32_t empty_addr = 0xFFFFFFFF;

    // 寻找第一个空白坑位 (0xFF)
    for (; addr < META_BASE_ADDR + META_SECTOR_SIZE; addr += META_BLOCK_SIZE) 
    {
        W25Q64_ReadData(addr, (uint8_t*)&block, sizeof(MetaBlock_t));
        if (block.magic == 0xFFFFFFFF) {
            empty_addr = addr;
            break;
        }
    }

    // 如果整个扇区 256 个坑位全满了，执行一次擦除 (磨损均衡)
    if (empty_addr == 0xFFFFFFFF) {
        printf(">> [SYS] Meta Sector full, performing wear-level erase...\r\n");
        W25Q64_SectorErase(META_BASE_ADDR);
        empty_addr = META_BASE_ADDR;
    }

    // 组装最新指针并附加校验码防撕裂
    block.w_addr  = off_write_addr;
    block.r_addr  = off_read_addr;
    block.magic   = META_MAGIC;
    block.xor_chk = block.w_addr ^ block.r_addr ^ block.magic;

    W25Q64_PageProgram(empty_addr, (uint8_t*)&block, sizeof(MetaBlock_t));
}
static void Flash_Append_Record(const char* record) 
{
    if ((off_write_addr % 4096) == 0) {
        W25Q64_SectorErase(off_write_addr); 
    }
    uint8_t buffer[RECORD_SIZE] = {0};
    strncpy((char*)buffer, record, RECORD_SIZE - 1);
    W25Q64_PageProgram(off_write_addr, buffer, RECORD_SIZE);
    printf(">> [FLASH] Offline Data Saved: %s", record);
    
    off_write_addr += RECORD_SIZE;
    if (off_write_addr >= OFFLINE_MAX_ADDR) off_write_addr = OFFLINE_BASE_ADDR; 
    
    // 🚨 写指针推进后，立刻持久化元数据！
    Save_Offline_Pointers(); 
}

typedef enum {
    ESP_STATE_SEND_AT = 0,      
    ESP_STATE_WAIT_AT,          
    ESP_STATE_SEND_CWMODE,      
    ESP_STATE_WAIT_CWMODE,      
    ESP_STATE_SEND_CWJAP,       
    ESP_STATE_WAIT_CWJAP,       
    ESP_STATE_SEND_CIPSTART,    
    ESP_STATE_WAIT_CIPSTART,    
    
    ESP_STATE_WORK_IDLE,         
    ESP_STATE_WORK_SEND_CIPSEND, 
    ESP_STATE_WORK_WAIT_PROMPT,  
    ESP_STATE_WORK_SEND_PAYLOAD, // 💡 架构师补丁：新增独立的载荷发射状态
    ESP_STATE_WORK_WAIT_SEND_OK  
} ESP8266_State_t;

int main(void)
{
    ESP8266_State_t esp_state = ESP_STATE_SEND_AT; 
    char esp_tx_buf[128];      
    char payload_buf[128];     
    char Process_Buffer[256];  

    uint32_t state_start_time = 0; 
    uint32_t last_sensor_time = 0; 
    uint8_t  fail_count = 0;   
    
    float temp = 0.0f, hum = 0.0f;
    bool new_msg_received = false; 

    Board_Init(); 
    W25Q64_Init(); 

    printf("\r\n======================================\r\n");
    printf("  Architect Level IoT Gateway Booting... \r\n");
    printf("  [+] Self-Recovery State Machine Active \r\n");
    printf("======================================\r\n");
    Load_Offline_Pointers();
    while (1)
    {
        new_msg_received = false;

        // ==========================================
        // 🔒 临界区与分级故障捕获 (Layered Demotion)
        // ==========================================
        if (UART2_RX_Flag == 1) 
        {
            __disable_irq(); 
            uint16_t copy_len = UART2_RX_Len;
            if (copy_len >= sizeof(Process_Buffer)) copy_len = sizeof(Process_Buffer) - 1;
            memcpy(Process_Buffer, UART2_RX_Buffer, copy_len);
            Process_Buffer[copy_len] = '\0'; 
            UART2_RX_Flag = 0; 
            __enable_irq();  
            
            printf("<< [ESP8266]: %s", Process_Buffer); 
            new_msg_received = true;

            // 🛡️ 架构师补丁：精准的降级打击，不再一刀切
            if (strstr(Process_Buffer, "WIFI DISCONNECT")) {
                printf("\r\n=> [ALARM] Wi-Fi Link Broken! Demoting to CWJAP...\r\n");
                esp_state = ESP_STATE_SEND_CWJAP; // 降级到连 Wi-Fi
                fail_count = 0;
                new_msg_received = false; 
            }
            else if (strstr(Process_Buffer, "CLOSED")) {
                printf("\r\n=> [ALARM] TCP Server Dropped! Demoting to CIPSTART...\r\n");
                // 如果已经在连 Wi-Fi 甚至更底层，就不理会 CLOSED
                if (esp_state >= ESP_STATE_SEND_CIPSTART) {
                    esp_state = ESP_STATE_SEND_CIPSTART; // 降级到连 TCP
                    fail_count = 0;
                    new_msg_received = false;
                }
            }
        }

        // ==========================================
        // 🛡️ 独立任务：雷打不动的传感器采集与离线判定
        // ==========================================
        if (g_RunTimeMs - last_sensor_time >= 5000) 
        {
            last_sensor_time = g_RunTimeMs;
            if (SHTC3_Get_Temp_Humi(&temp, &hum)) {
                char current_record[RECORD_SIZE];
                snprintf(current_record, RECORD_SIZE, "T:%.2f,H:%.2f\r\n", temp, hum);
                
                if (esp_state == ESP_STATE_WORK_IDLE && off_read_addr == off_write_addr) {
                    strcpy(payload_buf, current_record);
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND; 
                } else {
                    Flash_Append_Record(current_record);
                }
            }
        }

        // ==========================================
        // ⚙️ 大脑：带快失败机制的异步网络状态机
        // ==========================================
        switch (esp_state)
        {
            case ESP_STATE_SEND_AT:
                if (UART2_DMA_Send_Safe((uint8_t *)"AT\r\n", 4)) {
                    esp_state = ESP_STATE_WAIT_AT; state_start_time = g_RunTimeMs; 
                }
                break;

            case ESP_STATE_WAIT_AT:
                if (new_msg_received && strstr(Process_Buffer, "OK")) { esp_state = ESP_STATE_SEND_CWMODE; fail_count = 0;} 
                else if (g_RunTimeMs - state_start_time > 1000) { esp_state = ESP_STATE_SEND_AT; }
                break;

            case ESP_STATE_SEND_CWMODE:
                if (UART2_DMA_Send_Safe((uint8_t *)"AT+CWMODE=1\r\n", 13)) {
                    esp_state = ESP_STATE_WAIT_CWMODE; state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CWMODE:
                if (new_msg_received && strstr(Process_Buffer, "OK")) { esp_state = ESP_STATE_SEND_CWJAP; }
                else if (g_RunTimeMs - state_start_time > 1000) { esp_state = ESP_STATE_SEND_CWMODE; }
                break;

            case ESP_STATE_SEND_CWJAP:
                sprintf(esp_tx_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PWD);
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    esp_state = ESP_STATE_WAIT_CWJAP; state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CWJAP:
                if (new_msg_received && (strstr(Process_Buffer, "WIFI GOT IP") || strstr(Process_Buffer, "OK"))) {
                    esp_state = ESP_STATE_SEND_CIPSTART; fail_count = 0; 
                } 
                // 🚀 快失败捕获：如果热点密码错或找不到，别等 15 秒了，立刻重试！
                else if (new_msg_received && (strstr(Process_Buffer, "FAIL") || strstr(Process_Buffer, "ERROR"))) {
                    printf("=> [WARN] CWJAP Failed! Fast Retrying...\r\n");
                    Delay_ms(1000); // 略微限流，防 CPU 抽风
                    esp_state = ESP_STATE_SEND_CWJAP;
                }
                else if (g_RunTimeMs - state_start_time > 15000) {
                    fail_count++; 
                    if (fail_count >= 3) { esp_state = ESP_STATE_SEND_AT; fail_count = 0; } 
                    else { esp_state = ESP_STATE_SEND_CWJAP; }
                }
                break;

            case ESP_STATE_SEND_CIPSTART:
                sprintf(esp_tx_buf, "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n", TCP_IP, TCP_PORT);
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    esp_state = ESP_STATE_WAIT_CIPSTART; state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CIPSTART:
                if (new_msg_received && (strstr(Process_Buffer, "CONNECT") || strstr(Process_Buffer, "OK") || strstr(Process_Buffer, "ALREADY"))) {
                    esp_state = ESP_STATE_WORK_IDLE; fail_count = 0; 
                } 
                // 🚀 快失败捕获：如果连不上 TCP，立刻启动 CIPCLOSE 重建机制
                else if (new_msg_received && (strstr(Process_Buffer, "ERROR") || strstr(Process_Buffer, "FAIL"))) {
                    printf("=> [WARN] TCP Connect Error! Fast Retrying...\r\n");
                    UART2_DMA_Send_Safe((uint8_t *)"AT+CIPCLOSE\r\n", 13);
                    Delay_ms(100); 
                    esp_state = ESP_STATE_SEND_CIPSTART;
                }
                else if (g_RunTimeMs - state_start_time > 15000) {
                    fail_count++; 
                    if (fail_count >= 3) { esp_state = ESP_STATE_SEND_CWJAP; fail_count = 0; } 
                    else {
                        UART2_DMA_Send_Safe((uint8_t *)"AT+CIPCLOSE\r\n", 13);
                        Delay_ms(100); 
                        esp_state = ESP_STATE_SEND_CIPSTART;
                    }
                }
                break;

            // ------------------------------------------------------
            // 🚀 工作与断网续传区 (彻底修复状态吞噬 Bug)
            // ------------------------------------------------------
            case ESP_STATE_WORK_IDLE:
                if (off_read_addr != off_write_addr) 
                {
                    uint8_t buffer[RECORD_SIZE] = {0};
                    W25Q64_ReadData(off_read_addr, buffer, RECORD_SIZE);
                    strcpy(payload_buf, (char*)buffer);
                    printf(">> [RESUME] Trying history from 0x%08X...", off_read_addr);
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND;
                }
                break;

            case ESP_STATE_WORK_SEND_CIPSEND:
                sprintf(esp_tx_buf, "AT+CIPSEND=%d\r\n", strlen(payload_buf));
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    esp_state = ESP_STATE_WORK_WAIT_PROMPT;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WORK_WAIT_PROMPT:
                if (new_msg_received && strstr(Process_Buffer, ">")) {
                    // 🛡️ 架构师补丁：转移到专门的发送状态，即使 DMA 忙也不会弄丢 ">"
                    esp_state = ESP_STATE_WORK_SEND_PAYLOAD; 
                } 
                else if (new_msg_received && (strstr(Process_Buffer, "ERROR") || strstr(Process_Buffer, "link is not valid"))) {
                    esp_state = ESP_STATE_SEND_CIPSTART; // 连接半死，重建！
                }
                else if (g_RunTimeMs - state_start_time > 5000) {
                    esp_state = ESP_STATE_SEND_CIPSTART; 
                }
                break;

            // 🛡️ 架构师补丁：独立的物理发送态，只有真正推进了 DMA 才会跳转
            case ESP_STATE_WORK_SEND_PAYLOAD:
                if (UART2_DMA_Send_Safe((uint8_t *)payload_buf, strlen(payload_buf))) {
                    esp_state = ESP_STATE_WORK_WAIT_SEND_OK;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WORK_WAIT_SEND_OK:
                if (new_msg_received && strstr(Process_Buffer, "SEND OK")) {
                    esp_state = ESP_STATE_WORK_IDLE;
                    
                    if (off_read_addr != off_write_addr) {
                        off_read_addr += RECORD_SIZE;
                        if (off_read_addr >= OFFLINE_MAX_ADDR) off_read_addr = OFFLINE_BASE_ADDR;
                        if (off_read_addr == off_write_addr) {
                            off_read_addr = OFFLINE_BASE_ADDR; off_write_addr = OFFLINE_BASE_ADDR;
                            printf(">> [SYS] Backlog cleared! Queue reset.\r\n");
                        }
                        // 🚨 读指针推进(真正的出队)后，持久化元数据！
                        Save_Offline_Pointers();
                    }
                }
                // 🚀 快失败捕获：发送失败立刻重建，不卡 5 秒
                else if (new_msg_received && (strstr(Process_Buffer, "ERROR") || strstr(Process_Buffer, "SEND FAIL"))) {
                    printf("=> [WARN] Send Failed! Rebuilding TCP Link...\r\n");
                    esp_state = ESP_STATE_SEND_CIPSTART;
                }
                else if (g_RunTimeMs - state_start_time > 5000) {
                    esp_state = ESP_STATE_SEND_CIPSTART;
                }
                break;
        }
    }
}