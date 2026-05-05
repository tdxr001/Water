/**
  ******************************************************************************
  * @file   bsp_esp8266.c
  * @brief  wifi模组ESP-12F的驱动程序
  * 
  ******************************************************************************
  */
	
	//华清远见
#include "bsp_esp8266.h"
#include <stdarg.h>
#include "usart.h"
//
struct STRUCT_USART_Fram ESP8266_Fram_Record_Struct = { 0 };  //定义了一个数据帧结构体
volatile uint8_t TcpClosedFlag = 0;
static UART_HandleTypeDef *ESP8266_Uart = NULL;
static uint8_t *ESP8266_RxByte = NULL;

#define ESP8266_ALERT_RETRY_COUNT       5U
#define ESP8266_ALERT_RETRY_DELAY_MS    1200U
#define ESP8266_JOIN_STABLE_DELAY_MS    1200U
#define ESP8266_MQTT_PACKET_MAX_LEN     384U
#define ESP8266_MQTT_KEEPALIVE_SEC      60U

static void ESP8266_ClearRxBuffer(void);
static bool ESP8266_BufferContainsBytes(const uint8_t *needle, uint16_t needle_len);
static bool ESP8266_SendRawBytes(const uint8_t *data, uint16_t len, uint32_t wait_ms);
static uint16_t MQTT_EncodeRemainingLength(uint8_t *dst, uint32_t remaining_len);
static uint16_t MQTT_WriteUtf8(uint8_t *dst, uint16_t max_len, const char *text);
static uint16_t MQTT_BuildConnectPacket(uint8_t *packet, uint16_t max_len, const char *client_id);
static uint16_t MQTT_BuildPublishPacket(uint8_t *packet, uint16_t max_len,
                                        const char *topic, const char *payload);
static bool ESP8266_MQTTConnect(void);
static bool ESP8266_MQTTPublish(const char *topic, const char *payload);
static void ESP8266_MQTTDisconnect(void);
//初始化波特率
void ESP8266_Init(UART_HandleTypeDef *huart, uint8_t *DataBuf,uint32_t bound)
{
	ESP8266_Uart = huart;
	ESP8266_RxByte = DataBuf;

	//设置波特率
  huart->Init.BaudRate = bound;
	//初始化配置
  if (HAL_UART_Init(huart) != HAL_OK)
  {
    Error_Handler();
  }	
	//开启串口接收与空闲中断
	HAL_UART_Receive_IT(huart,(uint8_t *)DataBuf, 1);	//开启接收中断	
	__HAL_UART_CLEAR_IDLEFLAG(huart);			//清除空闲中断标志							
	__HAL_UART_ENABLE_IT(huart,UART_IT_IDLE);	//开启空闲中断	
}
//对ESP8266模块发送AT指令
// cmd 待发送的指令
// ack1,ack2;期待的响应，为NULL表不需响应，两者为或逻辑关系
// time 等待响应时间
//返回1发送成功， 0失败
bool ESP8266_Send_AT_Cmd(char *cmd,char *ack1,char *ack2,uint32_t time)
{
    ESP8266_Fram_Record_Struct.InfBit.FramFinishFlag = 0;
    ESP8266_Fram_Record_Struct.Data_RX_BUF[0] = '\0';
    ESP8266_Fram_Record_Struct .InfBit .FramLength = 0;		//重新接收新的数据包
    ESP8266_Fram_Record_Struct .InfBit .FramFinishFlag = 0;
    ESP8266_USART("%s\r\n", cmd);
    if(ack1==0&&ack2==0)		//不需要接收数据
    {
    return true;
    }
    HAL_Delay(time);	//延时
    ESP8266_Fram_Record_Struct.Data_RX_BUF[ESP8266_Fram_Record_Struct.InfBit.FramLength ] = '\0';
		//
    printf("%s",ESP8266_Fram_Record_Struct .Data_RX_BUF);
		//
    if((ack1!=0) && (ack2!=0))
    {
        return ( ( bool ) strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, ack1 ) || 
                         ( bool ) strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, ack2 ) );
    }
    else if( ack1 != 0 )  //strstr(s1,s2);检测s2是否为s1的一部分，是返回该位置，否则返回false，它强制转换为bool类型了
        return ( ( bool ) strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, ack1 ) );
    else
        return ( ( bool ) strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, ack2 ) );
}
//发送恢复出厂默认设置指令将模块恢复成出厂设置
void ESP8266_AT_Test(void)
{
    char count=0;
    HAL_Delay(100); 
    while(count < 10)
    {
        if(ESP8266_Send_AT_Cmd("AT+RESTORE","OK",NULL,1000)) 
        {
            printf("OK\r\n");
            return;
        }
        ++ count;
    }
}
//选择ESP8266的工作模式
// enumMode 模式类型
//成功返回true，失败返回false
bool ESP8266_Net_Mode_Choose(ENUM_Net_ModeTypeDef enumMode)
{
    switch ( enumMode )
    {
        case STA:
            return ESP8266_Send_AT_Cmd ( "AT+CWMODE=1", "OK", "no change", 2500 ); 

        case AP:
            return ESP8266_Send_AT_Cmd ( "AT+CWMODE=2", "OK", "no change", 2500 ); 

        case STA_AP:
            return ESP8266_Send_AT_Cmd ( "AT+CWMODE=3", "OK", "no change", 2500 ); 

        default:
          return false;
    }       
}
//ESP8266连接外部的WIFI
//pSSID WiFi帐号
//pPassWord WiFi密码
//设置成功返回true 反之false
bool ESP8266_JoinAP(char * pSSID, char * pPassWord)
{
    char cCmd [120];
		//
    sprintf ( cCmd, "AT+CWJAP=\"%s\",\"%s\"", pSSID, pPassWord );
    return ESP8266_Send_AT_Cmd( cCmd, "OK", NULL, 5000 );
}
//ESP8266 透传使能
//enumEnUnvarnishTx  是否多连接，bool类型
//设置成功返回true，反之false
bool ESP8266_Enable_MultipleId(FunctionalState enumEnUnvarnishTx )
{
    char cStr [20];

    sprintf ( cStr, "AT+CIPMUX=%d", ( enumEnUnvarnishTx ? 1 : 0 ) );

    return ESP8266_Send_AT_Cmd ( cStr, "OK", 0, 500 );
}
//ESP8266 连接服务器
//enumE  网络类型
//ip ，服务器IP
//ComNum  服务器端口
//id，连接号，确保通信不受外界干扰
//设置成功返回true，反之fasle
bool ESP8266_Link_Server(ENUM_NetPro_TypeDef enumE, char * ip, char * ComNum, ENUM_ID_NO_TypeDef id)
{
    char cStr [100] = { 0 }, cCmd [120];

    switch (  enumE )
    {
        case enumTCP:
          sprintf ( cStr, "\"%s\",\"%s\",%s", "TCP", ip, ComNum );
          break;

        case enumUDP:
          sprintf ( cStr, "\"%s\",\"%s\",%s", "UDP", ip, ComNum );
          break;

        default:
            break;
    }

    if ( id < 5 )
        sprintf ( cCmd, "AT+CIPSTART=%d,%s", id, cStr);

    else
        sprintf ( cCmd, "AT+CIPSTART=%s", cStr );

    return ESP8266_Send_AT_Cmd ( cCmd, "OK", "ALREADY CONNECT", 4000 ) ||
           ESP8266_Send_AT_Cmd ( cCmd, "OK", "ALREAY CONNECT", 4000 );
}
//透传使能
//设置成功返回true， 反之false
bool ESP8266_UnvarnishSend (void)
{
    if (!ESP8266_Send_AT_Cmd ( "AT+CIPMODE=1", "OK", 0, 500 ))
        return false;
    return 
        ESP8266_Send_AT_Cmd( "AT+CIPSEND", "OK", ">", 500 );
}
//ESP8266发送字符串
//enumEnUnvarnishTx是否使能透传模式
//pStr字符串
//ulStrLength字符串长度
//ucId 连接号
//设置成功返回true， 反之false
bool ESP8266_SendString(FunctionalState enumEnUnvarnishTx, char * pStr, uint32_t ulStrLength, ENUM_ID_NO_TypeDef ucId )
{
    char cStr [20];
    bool bRet = false;
		//
    if ( enumEnUnvarnishTx )
    {
        ESP8266_USART("%s", pStr );

        bRet = true;

    }
    else
    {
        if ( ucId < 5 )
            sprintf (cStr, "AT+CIPSEND=%d,%d", ucId, ulStrLength + 2 );

        else
            sprintf (cStr, "AT+CIPSEND=%d", ulStrLength + 2 );

        ESP8266_Send_AT_Cmd (cStr, "> ", 0, 1000 );

        bRet = ESP8266_Send_AT_Cmd (pStr, "SEND OK", 0, 1000 );
  }
    return bRet;
}
//ESP8266退出透传模式
void ESP8266_ExitUnvarnishSend (void)
{
    HAL_Delay(1000);
    ESP8266_USART("+++");
    HAL_Delay( 500 );    
}

void ESP8266_Close_TCP(void)
{
    (void)ESP8266_Send_AT_Cmd("AT+CIPCLOSE", "OK", "CLOSED", 1000);
}

bool ESP8266_TCPClient_Connect(void)
{
    uint8_t attempt;
    uint8_t status;

    /* 告警发送采用“一次连接、一次发送”的方式：
       旧测试函数会进入无限透传循环，不适合低功耗唤醒场景。这里每次唤醒后
       检查 TCP 状态，未连接时再连接 WiFi 和服务器，发送完成后由上层关闭连接。 */
    for (attempt = 0; attempt < ESP8266_ALERT_RETRY_COUNT; attempt++)
    {
        (void)ESP8266_Send_AT_Cmd("AT", "OK", NULL, 1000);
        if (!ESP8266_Net_Mode_Choose(STA))
        {
            HAL_Delay(ESP8266_ALERT_RETRY_DELAY_MS);
            continue;
        }

        /*
         * STM32 reset does not necessarily reset ESP8266.  A stale TCP socket can
         * make AT+CIPSTATUS report STATUS:3 while the MQTT session is no longer
         * usable, so every one-shot MQTT publish starts from a fresh TCP socket.
         */
        (void)ESP8266_Send_AT_Cmd("AT+CIPMODE=0", "OK", NULL, 500);
        (void)ESP8266_Enable_MultipleId(DISABLE);
        ESP8266_Close_TCP();

        status = ESP8266_Get_LinkStatus();
        if (status == 2 &&
            ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP,
                                User_ESP8266_TCPServer_PORT, Single_ID_0))
        {
            return true;
        }

        if (!ESP8266_JoinAP(User_ESP8266_SSID, User_ESP8266_PWD))
        {
            HAL_Delay(ESP8266_ALERT_RETRY_DELAY_MS);
            continue;
        }
        HAL_Delay(ESP8266_JOIN_STABLE_DELAY_MS);

        if (ESP8266_Enable_MultipleId(DISABLE) &&
            ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP,
                                User_ESP8266_TCPServer_PORT, Single_ID_0))
        {
            return true;
        }

        ESP8266_Close_TCP();
        HAL_Delay(ESP8266_ALERT_RETRY_DELAY_MS);
    }

    return false;
}

static void ESP8266_ClearRxBuffer(void)
{
    ESP8266_Fram_Record_Struct.InfBit.FramFinishFlag = 0;
    ESP8266_Fram_Record_Struct.InfBit.FramLength = 0;
    ESP8266_Fram_Record_Struct.Data_RX_BUF[0] = '\0';
}

static bool ESP8266_BufferContainsBytes(const uint8_t *needle, uint16_t needle_len)
{
    uint16_t i;
    uint16_t rx_len;

    if ((needle == NULL) || (needle_len == 0U))
    {
        return false;
    }

    rx_len = ESP8266_Fram_Record_Struct.InfBit.FramLength;
    if (rx_len < needle_len)
    {
        return false;
    }

    for (i = 0; i <= (uint16_t)(rx_len - needle_len); i++)
    {
        if (memcmp(&ESP8266_Fram_Record_Struct.Data_RX_BUF[i], needle, needle_len) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ESP8266_SendRawBytes(const uint8_t *data, uint16_t len, uint32_t wait_ms)
{
    char cStr[32];
    const uint8_t send_ok[] = {'S', 'E', 'N', 'D', ' ', 'O', 'K'};

    if ((ESP8266_Uart == NULL) || (data == NULL) || (len == 0U))
    {
        return false;
    }

    sprintf(cStr, "AT+CIPSEND=%u", len);
    if (!ESP8266_Send_AT_Cmd(cStr, ">", NULL, 1000))
    {
        return false;
    }

    ESP8266_ClearRxBuffer();
    if (HAL_UART_Transmit(ESP8266_Uart, (uint8_t *)data, len, 2000) != HAL_OK)
    {
        return false;
    }

    HAL_Delay(wait_ms);
    return ESP8266_BufferContainsBytes(send_ok, (uint16_t)sizeof(send_ok));
}

static uint16_t MQTT_EncodeRemainingLength(uint8_t *dst, uint32_t remaining_len)
{
    uint16_t encoded_len = 0;
    uint8_t encoded_byte;

    do
    {
        encoded_byte = (uint8_t)(remaining_len % 128U);
        remaining_len /= 128U;
        if (remaining_len > 0U)
        {
            encoded_byte |= 0x80U;
        }
        dst[encoded_len++] = encoded_byte;
    } while (remaining_len > 0U);

    return encoded_len;
}

static uint16_t MQTT_WriteUtf8(uint8_t *dst, uint16_t max_len, const char *text)
{
    uint16_t text_len;

    if (text == NULL)
    {
        text = "";
    }

    text_len = (uint16_t)strlen(text);
    if (max_len < (uint16_t)(text_len + 2U))
    {
        return 0;
    }

    dst[0] = (uint8_t)(text_len >> 8);
    dst[1] = (uint8_t)(text_len & 0xFFU);
    memcpy(&dst[2], text, text_len);

    return (uint16_t)(text_len + 2U);
}

static uint16_t MQTT_BuildConnectPacket(uint8_t *packet, uint16_t max_len, const char *client_id)
{
    uint16_t pos = 0;
    uint16_t client_field_len;
    uint32_t remaining_len;

    remaining_len = 10U + 2U + strlen(client_id);
    if (max_len < (uint16_t)(1U + 4U + remaining_len))
    {
        return 0;
    }

    packet[pos++] = 0x10U;
    pos += MQTT_EncodeRemainingLength(&packet[pos], remaining_len);

    packet[pos++] = 0x00U;
    packet[pos++] = 0x04U;
    packet[pos++] = 'M';
    packet[pos++] = 'Q';
    packet[pos++] = 'T';
    packet[pos++] = 'T';
    packet[pos++] = 0x04U;
    packet[pos++] = 0x02U;
    packet[pos++] = (uint8_t)(ESP8266_MQTT_KEEPALIVE_SEC >> 8);
    packet[pos++] = (uint8_t)(ESP8266_MQTT_KEEPALIVE_SEC & 0xFFU);

    client_field_len = MQTT_WriteUtf8(&packet[pos], (uint16_t)(max_len - pos), client_id);
    if (client_field_len == 0U)
    {
        return 0;
    }
    pos += client_field_len;

    return pos;
}

static uint16_t MQTT_BuildPublishPacket(uint8_t *packet, uint16_t max_len,
                                        const char *topic, const char *payload)
{
    uint16_t pos = 0;
    uint16_t topic_field_len;
    uint16_t payload_len;
    uint32_t remaining_len;

    if ((topic == NULL) || (payload == NULL))
    {
        return 0;
    }

    payload_len = (uint16_t)strlen(payload);
    remaining_len = 2U + strlen(topic) + payload_len;

    if (max_len < (uint16_t)(1U + 4U + remaining_len))
    {
        return 0;
    }

    packet[pos++] = 0x30U;
    pos += MQTT_EncodeRemainingLength(&packet[pos], remaining_len);

    topic_field_len = MQTT_WriteUtf8(&packet[pos], (uint16_t)(max_len - pos), topic);
    if (topic_field_len == 0U)
    {
        return 0;
    }
    pos += topic_field_len;

    memcpy(&packet[pos], payload, payload_len);
    pos += payload_len;

    return pos;
}

static bool ESP8266_MQTTConnect(void)
{
    uint8_t packet[ESP8266_MQTT_PACKET_MAX_LEN];
    uint16_t packet_len;
    const uint8_t connack_ok[] = {0x20U, 0x02U, 0x00U, 0x00U};

    packet_len = MQTT_BuildConnectPacket(packet, sizeof(packet), User_ESP8266_MQTT_CLIENT_ID);
    if (packet_len == 0U)
    {
        return false;
    }

    if (!ESP8266_TCPClient_Connect())
    {
        return false;
    }

    if (!ESP8266_SendRawBytes(packet, packet_len, 1500))
    {
        return false;
    }

    return ESP8266_BufferContainsBytes(connack_ok, (uint16_t)sizeof(connack_ok));
}

static bool ESP8266_MQTTPublish(const char *topic, const char *payload)
{
    uint8_t packet[ESP8266_MQTT_PACKET_MAX_LEN];
    uint16_t packet_len;

    packet_len = MQTT_BuildPublishPacket(packet, sizeof(packet), topic, payload);
    if (packet_len == 0U)
    {
        return false;
    }

    return ESP8266_SendRawBytes(packet, packet_len, 1000);
}

static void ESP8266_MQTTDisconnect(void)
{
    const uint8_t packet[] = {0xE0U, 0x00U};

    (void)ESP8266_SendRawBytes(packet, (uint16_t)sizeof(packet), 300);
}

bool ESP8266_SendAlert(const char *payload)
{
    uint8_t attempt;

    if (payload == NULL)
    {
        return false;
    }

    for (attempt = 0; attempt < ESP8266_ALERT_RETRY_COUNT; attempt++)
    {
        if (ESP8266_MQTTConnect() &&
            ESP8266_MQTTPublish(User_ESP8266_MQTT_TOPIC_ALERT, payload))
        {
            ESP8266_MQTTDisconnect();
            ESP8266_Close_TCP();
            return true;
        }

        ESP8266_Close_TCP();
        HAL_Delay(ESP8266_ALERT_RETRY_DELAY_MS);
    }

    return false;
}

void ESP8266_RxCpltCallback(UART_HandleTypeDef *huart, uint8_t data)
{
    if (huart != ESP8266_Uart)
    {
        return;
    }

    if (ESP8266_Fram_Record_Struct.InfBit.FramLength < (RX_BUF_MAX_LEN - 1))
    {
        ESP8266_Fram_Record_Struct.Data_RX_BUF[ESP8266_Fram_Record_Struct.InfBit.FramLength++] = (char)data;
        ESP8266_Fram_Record_Struct.Data_RX_BUF[ESP8266_Fram_Record_Struct.InfBit.FramLength] = '\0';
    }
    else
    {
        ESP8266_Fram_Record_Struct.InfBit.FramFinishFlag = 1;
    }

    if (strstr(ESP8266_Fram_Record_Struct.Data_RX_BUF, "CLOSED") != NULL)
    {
        TcpClosedFlag = 1;
    }

    if (ESP8266_RxByte != NULL)
    {
        HAL_UART_Receive_IT(huart, ESP8266_RxByte, 1);
    }
}

void ESP8266_IdleCallback(UART_HandleTypeDef *huart)
{
    if (huart != ESP8266_Uart)
    {
        return;
    }

    /* ESP8266 的 AT 回包长度不固定，用 UART 空闲中断把“一帧接收完成”
       标出来，ESP8266_Send_AT_Cmd 等待一段时间后即可搜索 OK/ERROR 等关键字。 */
    ESP8266_Fram_Record_Struct.Data_RX_BUF[ESP8266_Fram_Record_Struct.InfBit.FramLength] = '\0';
    ESP8266_Fram_Record_Struct.InfBit.FramFinishFlag = 1;
}
//ESP8266 检测连接状态
//返回0：获取状态失败
//返回2：获得ip
//返回3：建立连接 
//返回4：失去连接 
uint8_t ESP8266_Get_LinkStatus (void)
{
    if (ESP8266_Send_AT_Cmd( "AT+CIPSTATUS", "OK", 0, 500 ) )
    {
        if ( strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, "STATUS:2\r\n" ) )
            return 2;

        else if ( strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, "STATUS:3\r\n" ) )
            return 3;

        else if ( strstr ( ESP8266_Fram_Record_Struct .Data_RX_BUF, "STATUS:4\r\n" ) )
            return 4;       

    }

    return 0;
}
//
static char *itoa(int value, char *string, int radix )    //把一整数转换为字符串。
{
    int     i, d;
    int     flag = 0;
    char    *ptr = string;

    /* This implementation only works for decimal numbers. */
    if (radix != 10)
    {
        *ptr = 0;
        return string;
    }

    if (!value)
    {
        *ptr++ = 0x30;
        *ptr = 0;
        return string;
    }

    /* if this is a negative value insert the minus sign. */
    if (value < 0)
    {
        *ptr++ = '-';

        /* Make the value positive. */
        value *= -1;

    }

    for (i = 10000; i > 0; i /= 10)
    {
        d = value / i;

        if (d || flag)
        {
            *ptr++ = (char)(d + 0x30);
            value -= (d * i);
            flag = 1;
        }
    }

    /* Null terminate the string. */
    *ptr = 0;

    return string;

} /* NCL_Itoa */
//
void USART_printf(UART_HandleTypeDef * USARTx, char * Data, ... )
{
    const char *s;
    int d;   
    char buf[16];
	  char singleBuff[1];
		//
    va_list ap;
    va_start(ap, Data);
		//
    while ( * Data != 0 )     // 判断数据是否到达结束符
    {                                         
        if ( * Data == 0x5c )  //'\'
        {                                     
            switch ( *++Data )
            {
                case 'r':                                     //回车符
								singleBuff[0] = 0x0d;    
								__HAL_UART_CLEAR_FLAG(USARTx, UART_CLEAR_TCF);/* Clear the TC flag in the ICR register */
								HAL_UART_Transmit(USARTx, (void*)singleBuff, 1, 1);//阻塞式发送数据
                Data ++;
                break;

                case 'n':                                     //换行符
								singleBuff[0] = 0x0a;    
								__HAL_UART_CLEAR_FLAG(USARTx, UART_CLEAR_TCF);/* Clear the TC flag in the ICR register */
								HAL_UART_Transmit(USARTx, (void*)singleBuff, 1, 1);//阻塞式发送数据
                Data ++;
                break;

                default:
                Data ++;
                break;
            }            
        }
        else if ( * Data == '%')
        {                                     
            switch ( *++Data )
            {               
                case 's':                                         //字符串
                s = va_arg(ap, const char *);
                for ( ; *s; s++) 
                {
									singleBuff[0] = *s;    
									HAL_UART_Transmit(USARTx, (void*)singleBuff, 1, 1);//阻塞式发送数据
                  while(__HAL_UART_GET_FLAG(USARTx, UART_FLAG_TXE) == RESET);
                }
                Data++;
                break;

                case 'd':           
                d = va_arg(ap, int);
                itoa(d, buf, 10);
                for (s = buf; *s; s++) 
                {
									singleBuff[0] = *s;    
									HAL_UART_Transmit(USARTx, (void*)singleBuff, 1, 1);//阻塞式发送数据
                  while(__HAL_UART_GET_FLAG(USARTx, UART_FLAG_TXE) == RESET);
                }
                     Data++;
                     break;
                default:
                     Data++;
                     break;
            }        
        }
        else
				{
					singleBuff[0] = *Data++;    
					__HAL_UART_CLEAR_FLAG(USARTx, UART_CLEAR_TCF);/* Clear the TC flag in the ICR register */
					HAL_UART_Transmit(USARTx, (void*)singleBuff, 1, 1);//阻塞式发送数据
				}		
        while (__HAL_UART_GET_FLAG(USARTx, UART_FLAG_TXE) == RESET);
    }
}


//
void ESP8266_STA_TCPClient_Test(void)
{
    uint8_t res;
    char str[100]={0};
		printf("***************Restore the factory default mode***************\r\n");
    ESP8266_AT_Test();	//恢复出厂默认模式
		printf("***************The TCP mode is being configured***************\r\n");
    ESP8266_Net_Mode_Choose(STA);
    while(!ESP8266_JoinAP(User_ESP8266_SSID, User_ESP8266_PWD));
    ESP8266_Enable_MultipleId ( DISABLE );
    while(!ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP, User_ESP8266_TCPServer_PORT, Single_ID_0));
    while(!ESP8266_UnvarnishSend());
		printf("***************The TCP mode is configured. Procedure***************\r\n");
    while ( 1 )
    {       
			  sprintf (str,"Beijing HuaQing YuanJian education technology co., LTD\r\n" );//格式化发送字符串到TCP服务器
        ESP8266_SendString ( ENABLE, str, 0, Single_ID_0 );
        HAL_Delay(1000);
        if(TcpClosedFlag) //判断是否失去连接
        {
            ESP8266_ExitUnvarnishSend();	//退出透传模式
            do
            {
                res = ESP8266_Get_LinkStatus();	//获取连接状态
            }   
            while(!res);

            if(res == 4)	//确认失去连接，重连
            {
                while (!ESP8266_JoinAP(User_ESP8266_SSID, User_ESP8266_PWD ) );
                while (!ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP, User_ESP8266_TCPServer_PORT, Single_ID_0 ) );        
            } 
            while(!ESP8266_UnvarnishSend());                    
        }
    }   
}
//


