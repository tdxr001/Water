#include "net_test.h"
#include "bsp_esp8266.h"

void APP_NetworkTest_Init(UART_HandleTypeDef *huart, uint8_t *rx_buff)
{
    ESP8266_Init(huart, rx_buff, 115200);
}

void APP_NetworkTest_Run(void)
{
    (void)ESP8266_SendAlert("NET_TEST:ESP8266 mqtt online,broker=172.20.10.6:1883\r\n");
    HAL_Delay(5000);
}
