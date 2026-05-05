#ifndef __APP_NETWORK_TEST_H
#define __APP_NETWORK_TEST_H

#include "main.h"

#define APP_NET_TEST_ONLY 0

void APP_NetworkTest_Init(UART_HandleTypeDef *huart, uint8_t *rx_buff);
void APP_NetworkTest_Run(void);

#endif
