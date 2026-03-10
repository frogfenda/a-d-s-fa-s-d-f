#include "Board_init.h"
#include <string.h>

extern volatile uint32_t g_RunTimeMs; // 引入系统心跳

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

ESP8266_State_t esp_state = ESP_STATE_SEND_AT; 

#define WIFI_SSID "qingwafenda"
#define WIFI_PWD  "frogfenda"
#define TCP_IP    "192.168.1.100" 
#define TCP_PORT  "8080"          

char esp_tx_buf[128];      
char payload_buf[128];     
char Process_Buffer[256];  // 💡 架构师增加：主循环专属安全解析区

uint32_t state_start_time = 0; // 💡 记录状态切入的时间点
uint32_t last_sensor_time = 0; // 💡 记录上次传感器发送的时间
uint8_t  fail_count = 0;   

int main(void)
{
    float temp, hum;
    bool new_msg_received = false; // 本次循环是否有新消息标志

    Board_Init(); 
    
    // 🚨 务必确保你的 Board_Init() 里面初始化了 SysTick 产生 1ms 中断
    // SysTick_Config(SystemCoreClock / 1000); 

    printf("\r\n======================================\r\n");
    printf("  Industrial IoT Gateway Booting...    \r\n");
    printf("  Non-blocking State Machine Active!   \r\n");
    printf("======================================\r\n");

    while (1)
    {
        new_msg_received = false; // 默认本次循环没收到新消息

        // ==========================================================
        // 【数据安全转移】：临界区原子拷贝
        // ==========================================================
        if (UART2_RX_Flag == 1) 
        {
            __disable_irq(); // 🔒 关中断，防止 DMA 这时候插一脚
            
            uint16_t copy_len = UART2_RX_Len;
            if (copy_len >= sizeof(Process_Buffer)) copy_len = sizeof(Process_Buffer) - 1;
            
            memcpy(Process_Buffer, UART2_RX_Buffer, copy_len);
            Process_Buffer[copy_len] = '\0'; // 安全封口
            
            UART2_RX_Flag = 0; // 清理战场，让 DMA 可以继续报警
            UART2_RX_Len = 0;
            
            __enable_irq();  // 🔓 开中断
            
            printf("<< [ESP8266]: %s", Process_Buffer); 
            new_msg_received = true;

            // 🚨 全局网络异常捕获：一旦触发，立刻复位状态机
            if (strstr(Process_Buffer, "CLOSED") || strstr(Process_Buffer, "WIFI DISCONNECT")) {
                printf("=> [ALARM] Network Down! Forcing Reconnect...\r\n");
                esp_state = ESP_STATE_SEND_AT; // 退回原点最安全
                fail_count = 0;
                new_msg_received = false; // 清除标志，防止下面状态机误判
            }
        }

        // ==========================================================
        // 【核心大脑】：基于时间的非阻塞状态机
        // ==========================================================
        switch (esp_state)
        {
            case ESP_STATE_SEND_AT:
                if (UART2_DMA_Send_Safe((uint8_t *)"AT\r\n", 4)) {
                    printf(">> [STATE] Sending AT...\r\n");
                    esp_state = ESP_STATE_WAIT_AT; 
                    state_start_time = g_RunTimeMs; // 记录发出的时间
                }
                break;

            case ESP_STATE_WAIT_AT:
                // 有新消息且包含 OK
                if (new_msg_received && strstr(Process_Buffer, "OK")) {
                    printf("=> [OK] AT Handshake Success!\r\n");
                    esp_state = ESP_STATE_SEND_CWMODE; 
                }
                // 超时判断：当前时间 - 开始时间 > 1000 毫秒 (1秒)
                else if (g_RunTimeMs - state_start_time > 1000) {
                    esp_state = ESP_STATE_SEND_AT; 
                }
                break;

            case ESP_STATE_SEND_CWMODE:
                if (UART2_DMA_Send_Safe((uint8_t *)"AT+CWMODE=1\r\n", 13)) {
                    printf(">> [STATE] Setting STA Mode...\r\n");
                    esp_state = ESP_STATE_WAIT_CWMODE;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CWMODE:
                if (new_msg_received && strstr(Process_Buffer, "OK")) {
                    printf("=> [OK] Mode Set to STA!\r\n");
                    esp_state = ESP_STATE_SEND_CWJAP;
                }
                else if (g_RunTimeMs - state_start_time > 1000) {
                    esp_state = ESP_STATE_SEND_CWMODE;
                }
                break;

            case ESP_STATE_SEND_CWJAP:
                sprintf(esp_tx_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PWD);
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    printf(">> [STATE] Connecting to WiFi...\r\n");
                    esp_state = ESP_STATE_WAIT_CWJAP;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CWJAP:
                if (new_msg_received && strstr(Process_Buffer, "WIFI GOT IP")) {
                    printf("=> [OK] WiFi Connected & IP Got!\r\n");
                    esp_state = ESP_STATE_SEND_CIPSTART;
                    fail_count = 0; 
                }
                // 连接 WiFi 较慢，给 20 秒 (20000ms) 超时
                else if (g_RunTimeMs - state_start_time > 20000) {
                    fail_count++; 
                    printf("=> [WARN] WiFi Connect Timeout! Fail Count: %d\r\n", fail_count);
                    if (fail_count >= 3) {
                        printf("=> [FATAL] WiFi Dead! Restarting from AT...\r\n");
                        esp_state = ESP_STATE_SEND_AT; 
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_SEND_CWJAP; 
                    }
                }
                break;

            case ESP_STATE_SEND_CIPSTART:
                sprintf(esp_tx_buf, "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n", TCP_IP, TCP_PORT);
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    printf(">> [STATE] Connecting to TCP Server...\r\n");
                    esp_state = ESP_STATE_WAIT_CIPSTART;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WAIT_CIPSTART:
                if (new_msg_received && (strstr(Process_Buffer, "CONNECT") || strstr(Process_Buffer, "OK"))) {
                    printf("=> [OK] TCP Server Connected! Enter IDLE mode.\r\n");
                    esp_state = ESP_STATE_WORK_IDLE; 
                    fail_count = 0; 
                    last_sensor_time = g_RunTimeMs; // 重置工作区定时器
                }
                else if (g_RunTimeMs - state_start_time > 5000) { // 5秒超时
                    fail_count++; 
                    printf("=> [WARN] TCP Connect Timeout! Fail Count: %d\r\n", fail_count);
                    if (fail_count >= 3) {
                        esp_state = ESP_STATE_SEND_CWJAP; 
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_SEND_CIPSTART;
                    }
                }
                break;

            // ------------------------------------------------------
            // 工作区：利用时间戳取代 Delay
            // ------------------------------------------------------
            case ESP_STATE_WORK_IDLE:
                // 检查是否距离上次发送过去了 5 秒 (5000ms)
                if (g_RunTimeMs - last_sensor_time >= 5000) {
                    last_sensor_time = g_RunTimeMs;
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND;
                }
                break;

            case ESP_STATE_WORK_SEND_CIPSEND:
                // 1. 采集数据 (假设此函数内部没有极长的死等阻塞)
                SHT30_Get_Temp_Humi(&temp, &hum);
                printf("\r\n[Sensor] Read Temp: %.2f, Hum: %.2f\r\n", temp, hum);
                sprintf(payload_buf, "Temp:%.2f,Hum:%.2f\r\n", temp, hum);
                
                sprintf(esp_tx_buf, "AT+CIPSEND=%d\r\n", strlen(payload_buf));
                if (UART2_DMA_Send_Safe((uint8_t *)esp_tx_buf, strlen(esp_tx_buf))) {
                    printf(">> [STATE] Requesting CIPSEND (%d bytes)...\r\n", strlen(payload_buf));
                    esp_state = ESP_STATE_WORK_WAIT_PROMPT;
                    state_start_time = g_RunTimeMs;
                }
                break;

            case ESP_STATE_WORK_WAIT_PROMPT:
                if (new_msg_received && strstr(Process_Buffer, ">")) {
                    printf("=> [OK] Got '>', sending real payload!\r\n");
                    if (UART2_DMA_Send_Safe((uint8_t *)payload_buf, strlen(payload_buf))) {
                        esp_state = ESP_STATE_WORK_WAIT_SEND_OK;
                        state_start_time = g_RunTimeMs;
                    }
                }
                else if (g_RunTimeMs - state_start_time > 5000) { // 5秒超时
                    fail_count++;
                    if (fail_count >= 3) {
                        esp_state = ESP_STATE_SEND_CIPSTART; 
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_WORK_IDLE; 
                    }
                }
                break;

            case ESP_STATE_WORK_WAIT_SEND_OK:
                if (new_msg_received && strstr(Process_Buffer, "SEND OK")) {
                    printf("=> [SUCCESS] Payload sent to cloud! Back to IDLE.\r\n");
                    esp_state = ESP_STATE_WORK_IDLE;
                    fail_count = 0; 
                }
                else if (g_RunTimeMs - state_start_time > 5000) { // 5秒超时
                    fail_count++;
                    if (fail_count >= 3) {
                        esp_state = ESP_STATE_SEND_CIPSTART;
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_WORK_IDLE;
                    }
                }
                break;
        }

        // 🚨 这里绝不再有任何 Delay_ms()！CPU 将全速狂奔 (Free Running)
        // 任何其他的任务 (按键检测、LCD刷新) 都可以无缝加入此 while(1) 中
    }
}