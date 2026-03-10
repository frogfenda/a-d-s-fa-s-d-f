#include "Board_init.h"

// ==========================================================
// 💡 架构师级：ESP8266 完整生命周期状态机
// ==========================================================
typedef enum {
    ESP_STATE_SEND_AT = 0,      
    ESP_STATE_WAIT_AT,          
    ESP_STATE_SEND_CWMODE,      
    ESP_STATE_WAIT_CWMODE,      
    ESP_STATE_SEND_CWJAP,       
    ESP_STATE_WAIT_CWJAP,       
    ESP_STATE_SEND_CIPSTART,    
    ESP_STATE_WAIT_CIPSTART,    
    
    ESP_STATE_WORK_IDLE,         // 挂机养老区
    ESP_STATE_WORK_SEND_CIPSEND, // 发送数据长度打报告
    ESP_STATE_WORK_WAIT_PROMPT,  // 等待 > 符号准许
    ESP_STATE_WORK_WAIT_SEND_OK  // 等待云端确认接收
} ESP8266_State_t;

ESP8266_State_t esp_state = ESP_STATE_SEND_AT; 

// 🚨 请在这里填入你真实的 WiFi 和 电脑上的 TCP 服务器 IP
#define WIFI_SSID "qingwafenda"
#define WIFI_PWD  "frogfenda"
#define TCP_IP    "192.168.1.100" 
#define TCP_PORT  "8080"          

// ==========================================================
// 💡 全局工作缓存与控制变量
// ==========================================================
char esp_tx_buf[128];      // 发送 AT 指令的拼装舱
char payload_buf[128];     // 发送实体传感器数据的拼装舱
uint16_t wait_timeout = 0; // 状态超时计数器 (每加1代表0.1秒)
uint16_t sensor_timer = 0; // 传感器采集周期定时器
uint8_t  fail_count = 0;   // 连续失败计数器 (事不过三法则)

int main(void)
{
    float temp, hum;
    
    // 1. 拉下总闸，唤醒所有底层硬件 (听诊器和专线同时就位)
    Board_Init(); 

    // 这些通过 USART1 发给电脑，完全不会干扰 ESP8266
    printf("\r\n======================================\r\n");
    printf("  Industrial IoT Gateway Booting...    \r\n");
    printf("  Dual-UART & State Machine Active!    \r\n");
    printf("======================================\r\n");

    while (1)
    {
        // ==========================================================
        // 【打扫专线战场】：处理 USART2 收到的 ESP8266 消息
        // ==========================================================
        if (UART2_RX_Flag == 1) {
            // 安全封口
            if (UART2_RX_Len < RX_MAX_LEN) UART2_RX_Buffer[UART2_RX_Len] = '\0'; 
            else UART2_RX_Buffer[RX_MAX_LEN - 1] = '\0'; 
            
            // 用听诊器打印模块回传的机密对话
            printf("<< [ESP8266]: %s", UART2_RX_Buffer); 

            // ------------------------------------------------------
            // 🚨 架构师的上帝之手：【全局网络异常捕获】
            // 不管当前在干嘛，只要断网，瞬间踢回重连状态！
            // ------------------------------------------------------
            if (strstr((char *)UART2_RX_Buffer, "CLOSED")) {
                printf("=> [ALARM] TCP Closed by Server! Forcing Reconnect...\r\n");
                esp_state = ESP_STATE_SEND_CIPSTART; 
                fail_count = 0;
            }
            else if (strstr((char *)UART2_RX_Buffer, "WIFI DISCONNECT")) {
                printf("=> [ALARM] WiFi Disconnected! Forcing Reconnect...\r\n");
                esp_state = ESP_STATE_SEND_CWJAP; 
                fail_count = 0;
            }
        }

        // ==========================================================
        // 【核心大脑】：异步状态机流转
        // ==========================================================
        switch (esp_state)
        {
            case ESP_STATE_SEND_AT:
                printf(">> [STATE] Sending AT...\r\n");
                UART2_DMA_Send((uint8_t *)"AT\r\n", 4); 
                esp_state = ESP_STATE_WAIT_AT; 
                wait_timeout = 0;              
                break;

            case ESP_STATE_WAIT_AT:
                if (UART2_RX_Flag && strstr((char *)UART2_RX_Buffer, "OK")) {
                    printf("=> [OK] AT Handshake Success!\r\n");
                    esp_state = ESP_STATE_SEND_CWMODE; 
                }
                if (++wait_timeout > 10) esp_state = ESP_STATE_SEND_AT; 
                break;

            case ESP_STATE_SEND_CWMODE:
                printf(">> [STATE] Setting STA Mode...\r\n");
                UART2_DMA_Send((uint8_t *)"AT+CWMODE=1\r\n", 13);
                esp_state = ESP_STATE_WAIT_CWMODE;
                wait_timeout = 0;
                break;

            case ESP_STATE_WAIT_CWMODE:
                if (UART2_RX_Flag && strstr((char *)UART2_RX_Buffer, "OK")) {
                    printf("=> [OK] Mode Set to STA!\r\n");
                    esp_state = ESP_STATE_SEND_CWJAP;
                }
                if (++wait_timeout > 10) esp_state = ESP_STATE_SEND_CWMODE;
                break;

            case ESP_STATE_SEND_CWJAP:
                printf(">> [STATE] Connecting to WiFi...\r\n");
                sprintf(esp_tx_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PWD);
                UART2_DMA_Send((uint8_t *)esp_tx_buf, strlen(esp_tx_buf));
                esp_state = ESP_STATE_WAIT_CWJAP;
                wait_timeout = 0;
                break;

            case ESP_STATE_WAIT_CWJAP:
                // 只要拿到 IP 就代表彻底连上路由器了
                if (UART2_RX_Flag && strstr((char *)UART2_RX_Buffer, "WIFI GOT IP")) {
                    printf("=> [OK] WiFi Connected & IP Got!\r\n");
                    esp_state = ESP_STATE_SEND_CIPSTART;
                    fail_count = 0; // 🚨 连网成功，洗白记过记录
                }
                
                // 超时处理 (这里改成 200，即等待 20 秒)
                if (++wait_timeout > 200) {
                    fail_count++; // 🚨 记过 1 次！
                    printf("=> [WARN] WiFi Connect Timeout! Fail Count: %d\r\n", fail_count);
                    
                    if (fail_count >= 3) {
                        // 事不过三！直接判定模块死锁，从头开始重新唤醒！
                        printf("=> [FATAL] WiFi Dead! Restarting from AT...\r\n");
                        esp_state = ESP_STATE_SEND_AT; 
                        fail_count = 0;
                    } else {
                        // 还没到 3 次，再发一次连 WiFi 指令试试
                        esp_state = ESP_STATE_SEND_CWJAP; 
                    }
                }
                break;
            case ESP_STATE_SEND_CIPSTART:
                printf(">> [STATE] Connecting to TCP Server...\r\n");
                sprintf(esp_tx_buf, "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n", TCP_IP, TCP_PORT);
                UART2_DMA_Send((uint8_t *)esp_tx_buf, strlen(esp_tx_buf));
                esp_state = ESP_STATE_WAIT_CIPSTART;
                wait_timeout = 0;
                break;

            case ESP_STATE_WAIT_CIPSTART:
                if (UART2_RX_Flag && (strstr((char *)UART2_RX_Buffer, "CONNECT") || strstr((char *)UART2_RX_Buffer, "OK"))) {
                    printf("=> [OK] TCP Server Connected! Enter IDLE mode.\r\n");
                    esp_state = ESP_STATE_WORK_IDLE; 
                    fail_count = 0; // 🚨 连网成功，洗白记过记录
                }
                
                // 超时处理 (等待 5 秒)
                if (++wait_timeout > 50) {
                    fail_count++; // 🚨 记过 1 次！
                    printf("=> [WARN] TCP Connect Timeout! Fail Count: %d\r\n", fail_count);
                    
                    if (fail_count >= 3) {
                        // 事不过三！服务器可能挂了，退回上一级去检查 WiFi！
                        printf("=> [FATAL] TCP Dead! Checking WiFi...\r\n");
                        esp_state = ESP_STATE_SEND_CWJAP; 
                        fail_count = 0;
                    } else {
                        // 还没到 3 次，再发一次连 TCP 指令试试
                        esp_state = ESP_STATE_SEND_CIPSTART;
                    }
                }
                break;
            // ------------------------------------------------------
            // 工作区：定时发送传感器数据
            // ------------------------------------------------------
            case ESP_STATE_WORK_IDLE:
                // 每隔 50 个周期 (5秒) 触发一次发送
                if (++sensor_timer > 50) {
                    sensor_timer = 0;
                    esp_state = ESP_STATE_WORK_SEND_CIPSEND;
                }
                break;

            case ESP_STATE_WORK_SEND_CIPSEND:
                // 1. 采集最新温湿度并打包
                SHT30_Get_Temp_Humi(&temp, &hum);
                printf("\r\n[Sensor] Read Temp: %.2f, Hum: %.2f\r\n", temp, hum);
                sprintf(payload_buf, "Temp:%.2f,Hum:%.2f\r\n", temp, hum);
                
                // 2. 告诉 ESP8266 准备接收多少数据
                printf(">> [STATE] Requesting CIPSEND (%d bytes)...\r\n", strlen(payload_buf));
                sprintf(esp_tx_buf, "AT+CIPSEND=%d\r\n", strlen(payload_buf));
                UART2_DMA_Send((uint8_t *)esp_tx_buf, strlen(esp_tx_buf));
                
                esp_state = ESP_STATE_WORK_WAIT_PROMPT;
                wait_timeout = 0;
                break;

            case ESP_STATE_WORK_WAIT_PROMPT:
                // 等待准许符 ">"
                if (UART2_RX_Flag && strstr((char *)UART2_RX_Buffer, ">")) {
                    printf("=> [OK] Got '>', sending real payload!\r\n");
                    UART2_DMA_Send((uint8_t *)payload_buf, strlen(payload_buf));
                    esp_state = ESP_STATE_WORK_WAIT_SEND_OK;
                    wait_timeout = 0;
                }
                // 超时处理：事不过三机制
                if (++wait_timeout > 50) {
                    fail_count++;
                    printf("=> [WARN] Wait '>' Timeout! Fail Count: %d\r\n", fail_count);
                    if (fail_count >= 3) {
                        printf("=> [FATAL] Link Dead! Rebooting TCP...\r\n");
                        esp_state = ESP_STATE_SEND_CIPSTART; 
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_WORK_IDLE; // 退回空闲，下次再试
                    }
                }
                break;

            case ESP_STATE_WORK_WAIT_SEND_OK:
                if (UART2_RX_Flag && strstr((char *)UART2_RX_Buffer, "SEND OK")) {
                    printf("=> [SUCCESS] Payload sent to cloud! Back to IDLE.\r\n");
                    esp_state = ESP_STATE_WORK_IDLE;
                    fail_count = 0; // 发送成功，清零记过记录
                }
                if (++wait_timeout > 50) {
                    fail_count++;
                    printf("=> [WARN] Wait SEND_OK Timeout! Fail Count: %d\r\n", fail_count);
                    if (fail_count >= 3) {
                        printf("=> [FATAL] Link Dead! Rebooting TCP...\r\n");
                        esp_state = ESP_STATE_SEND_CIPSTART;
                        fail_count = 0;
                    } else {
                        esp_state = ESP_STATE_WORK_IDLE;
                    }
                }
                break;
        }

        // ==========================================================
        // 【清理战场与心跳】
        // ==========================================================
        UART2_RX_Flag = 0; 
        UART2_RX_Len = 0;

        // 整个网关的心跳节拍：100ms
        Delay_ms(100); 
    }
}