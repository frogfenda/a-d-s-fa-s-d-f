#include "stm32f10x.h"
#include "Board_init.h"
#include "Sys_tik.h"
#include "usart.h"
#include "shtc3.h"
#include "w25q64.h"  // 🚨 引入 SPI Flash 驱动
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define WIFI_SSID "Your_2.4G_WIFI_Name"
#define WIFI_PWD  "Your_Password"
#define TCP_IP    "192.168.1.100" 
#define TCP_PORT  "8080"          

// ==================================================
// 💾 架构师级：离线缓存队列物理模型定义
// ==================================================
#define OFFLINE_BASE_ADDR   0x00100000  // 预留前 1MB 给代码/Bootloader，从第 1MB 开始用
#define OFFLINE_MAX_ADDR    0x00200000  // 划拨 1MB 空间作为断网续传区 (可存 32768 条记录!)
#define RECORD_SIZE         32          // 🚨 必须是 256 (Page大小) 的约数，防止跨页覆盖!

// 内存中的队列指针 (为了防磨损，裸机简易版保存在 RAM 中，重启会清空积压队列)
uint32_t off_write_addr = OFFLINE_BASE_ADDR; 
uint32_t off_read_addr  = OFFLINE_BASE_ADDR;

// 内部函数：向 Flash 压入一条断网数据
static void Flash_Append_Record(const char* record) 
{
    // 坑点防御：如果写指针刚好落在一个扇区(4KB)的起始地址，必须先擦除该扇区
    if ((off_write_addr % 4096) == 0) {
        printf(">> [FLASH] Sector Boundary Reached. Erasing Sector at 0x%08X...\r\n", off_write_addr);
        W25Q64_SectorErase(off_write_addr); // ⚠️ 此处会有大约几十ms的硬件阻塞
    }
    
    // 构造定长缓存区，不足补0
    uint8_t buffer[RECORD_SIZE] = {0};
    strncpy((char*)buffer, record, RECORD_SIZE - 1);
    
    W25Q64_PageProgram(off_write_addr, buffer, RECORD_SIZE);
    printf(">> [FLASH] Offline Data Saved to 0x%08X.\r\n", off_write_addr);
    
    off_write_addr += RECORD_SIZE;
    // 环形队列防越界
    if (off_write_addr >= OFFLINE_MAX_ADDR) off_write_addr = OFFLINE_BASE_ADDR; 
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
    W25Q64_Init(); // 🚨 唤醒你的 SPI Flash

    printf("\r\n======================================\r\n");
    printf("  Industrial IoT Gateway Booting...      \r\n");
    printf("  [+] Hardware CRC Active                \r\n");
    printf("  [+] Offline Resume Storage Active      \r\n");
    printf("======================================\r\n");

    while (1)
    {
        new_msg_received = false;

        // ------------------------------------------
        // 🔒 临界区：原子级拷贝 DMA 数据
        // ------------------------------------------
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

            if (strstr(Process_Buffer, "CLOSED") || strstr(Process_Buffer, "WIFI DISCONNECT")) {
                printf("\r\n=> [ALARM] Network Down! Gateway entering OFFLINE mode...\r\n");
                esp_state = ESP_STATE_SEND_AT; 
                fail_count = 0;
                new_msg_received = false; 
            }
        }

        // ==========================================
        // 🛡️ 独立任务：SHTC3 数据采集与离线判定
        // ==========================================
        // 不管网络是死是活，每 5 秒雷打不动地采集数据
        if (g_RunTimeMs - last_sensor_time >= 5000) 
        {
            last_sensor_time = g_RunTimeMs;
            
            if (SHTC3_Get_Temp_Humi(&temp, &hum)) {
                printf("\r\n[Sensor] T: %.2f C, H: %.2f %%\r\n", temp, hum);
                
                // 将数据格式化为严格的定长记录
                char current_record[RECORD_SIZE];
                snprintf(current_record, RECORD_SIZE, "T:%.2f,H:%.2f\r\n", temp, hum);
                
                // 架构师判定逻辑：
                // 如果当前网络空闲 (IDLE) 且 没有任何历史积压数据 (Read == Write)
                if (esp_state == ESP_STATE_WORK_IDLE && off_read_addr == off_write_addr) {
                    strcpy(payload_buf, current_record);
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND; // 直通云端
                } else {
                    // 否则 (网断了、正在连网、或者正在传之前的积压数据) -> 砸进 Flash
                    Flash_Append_Record(current_record);
                }
            } else {
                printf("\r\n=> [WARN] Sensor Error. Skipping...\r\n");
            }
        }

        // ==========================================
        // ⚙️ 大脑：异步网络状态机
        // ==========================================
        switch (esp_state)
        {
            case ESP_STATE_SEND_AT:
                if (UART2_DMA_Send_Safe((uint8_t *)"AT\r\n", 4)) {
                    esp_state = ESP_STATE_WAIT_AT; state_start_time = g_RunTimeMs; 
                }
                break;

            case ESP_STATE_WAIT_AT:
                if (new_msg_received && strstr(Process_Buffer, "OK")) {
                    esp_state = ESP_STATE_SEND_CWMODE; fail_count = 0;
                } else if (g_RunTimeMs - state_start_time > 1000) { esp_state = ESP_STATE_SEND_AT; }
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
                if (new_msg_received && strstr(Process_Buffer, "WIFI GOT IP")) {
                    esp_state = ESP_STATE_SEND_CIPSTART; fail_count = 0; 
                } else if (g_RunTimeMs - state_start_time > 15000) {
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
                if (new_msg_received && (strstr(Process_Buffer, "CONNECT") || strstr(Process_Buffer, "OK"))) {
                    esp_state = ESP_STATE_WORK_IDLE; fail_count = 0; 
                } else if (new_msg_received && strstr(Process_Buffer, "ALREADY CONNECTED")) {
                    esp_state = ESP_STATE_WORK_IDLE; fail_count = 0; 
                } else if (g_RunTimeMs - state_start_time > 15000) {
                    fail_count++; 
                    if (fail_count >= 3) { esp_state = ESP_STATE_SEND_CWJAP; fail_count = 0; } 
                    else {
                        UART2_DMA_Send_Safe((uint8_t *)"AT+CIPCLOSE\r\n", 13);
                        Delay_ms(50); 
                        esp_state = ESP_STATE_SEND_CIPSTART;
                    }
                }
                break;

            // ------------------------------------------------------
            // 🚀 高速上传区：断网续传的核心
            // ------------------------------------------------------
            case ESP_STATE_WORK_IDLE:
                // 网络空闲时，立刻检查 Flash 历史积压
                if (off_read_addr != off_write_addr) 
                {
                    uint8_t buffer[RECORD_SIZE] = {0};
                    W25Q64_ReadData(off_read_addr, buffer, RECORD_SIZE);
                    
                    // 将读出来的历史数据扔给发送缓冲区
                    strcpy(payload_buf, (char*)buffer);
                    printf(">> [RESUME] Read history from 0x%08X: %s", off_read_addr, payload_buf);
                    
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND;
                }
                // 如果没有积压，状态机就会停在这里，等待独立的传感器任务来触发
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
                    if (UART2_DMA_Send_Safe((uint8_t *)payload_buf, strlen(payload_buf))) {
                        esp_state = ESP_STATE_WORK_WAIT_SEND_OK;
                        state_start_time = g_RunTimeMs;
                    }
                } else if (g_RunTimeMs - state_start_time > 5000) {
                    esp_state = ESP_STATE_SEND_CIPSTART; // 连接可能半死，强制重建
                }
                break;

            case ESP_STATE_WORK_WAIT_SEND_OK:
                if (new_msg_received && strstr(Process_Buffer, "SEND OK")) {
                    esp_state = ESP_STATE_WORK_IDLE;
                    
                    // 💡 架构师注：数据被云端确认收到后，历史数据才能真正出队！
                    if (off_read_addr != off_write_addr) {
                        off_read_addr += RECORD_SIZE;
                        if (off_read_addr >= OFFLINE_MAX_ADDR) off_read_addr = OFFLINE_BASE_ADDR;
                        
                        // 优化：如果追平了，双指针清零重置，极大地延长 Flash 寿命
                        if (off_read_addr == off_write_addr) {
                            off_read_addr = OFFLINE_BASE_ADDR;
                            off_write_addr = OFFLINE_BASE_ADDR;
                            printf(">> [SYS] Backlog cleared! Queue reset.\r\n");
                        }
                    }
                } else if (g_RunTimeMs - state_start_time > 5000) {
                    // 超时没发成功，读指针不移动，下次 IDLE 依然会重传这条历史数据！(这叫 QoS 保证)
                    esp_state = ESP_STATE_SEND_CIPSTART;
                }
                break;
        }
    }
}