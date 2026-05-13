/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "icache.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lsm6dsr.h"
#include "net_test.h"
#include "low_power.h"
#include "bsp_esp8266.h"
#include "beep.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  const char *type;          
  uint8_t level;             
  int32_t peak;              // 本次事件最大变化值
  uint8_t can_use_count;     
  uint32_t time_ms;          //对比的时间，序列的对比
  uint8_t send;              // 是否发送网络警告
  uint8_t beep;              
} MotionResult_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SMP_NUM   12    // 采样点数
#define SMP_MS    10    // 采样间隔毫秒
#define ACT_MG    90    // 记为有效震动的最小变化值
#define TOUCH_MG  320   // touch1 的峰值上限
#define KNOCK_MG  1100   
#define TOUCH_CNT 5     // touch1 的最大有效点数
#define KNOCK_CNT 9      
#define TOUCH_MS  50    //touch1 的最大持续时间
#define KNOCK_MS  100   
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t gRX_Buff[1];
extern volatile uint8_t TcpClosedFlag;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void APP_HandleSensorWakeup(void);
static void SendMotionAlert(const MotionResult_t *result, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, uint8_t clear_wake, uint32_t delay_ms);
static void CheckMotion(MotionResult_t *result, int32_t *ax_mg, int32_t *ay_mg, int32_t *az_mg);
static void MarkMotion(MotionResult_t *result, int32_t peak, uint8_t can_use_count, uint32_t time_ms);
static int32_t APP_Abs32(int32_t value);
static int32_t APP_Max3(int32_t a, int32_t b, int32_t c);
static int32_t AccelRawToMg(int16_t raw);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
__ASM (".global __use_no_semihosting");

struct FILE
{
  int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
  x = x;
}

void _ttywrch(int ch)
{
  ch = ch;
}

int fputc(int ch, FILE *f)
{
  uint8_t temp[1] = {(uint8_t)ch};

  HAL_UART_Transmit(&huart1, temp, 1, 2);
  return ch;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_ICACHE_Init();
  MX_USART1_UART_Init();
  MX_UART5_Init();
  /* USER CODE BEGIN 2 */
#if (APP_NET_TEST_ONLY)
  APP_NetworkTest_Init(&huart5, (uint8_t *)gRX_Buff);
#else
  ESP8266_Init(&huart5, (uint8_t *)gRX_Buff, 115200);
  (void)ESP8266_SendAlert("BOOT:U575 tamper firmware online,broker=172.20.10.6:1883\r\n");

  if (LSM6DSR_Init() == 0)
  {
    char error_msg[96];
    snprintf(error_msg, sizeof(error_msg), "ERROR:LSM6DSR init failed,who_am_i=0x%02X,check wiring/address\r\n", LSM6DSR_Read_ID());
    (void)ESP8266_SendAlert(error_msg);
    Error_Handler();
  }

  if (LSM6DSR_Config_Wakeup_INT1() == 0)
  {
    (void)ESP8266_SendAlert("ERROR:LSM6DSR wakeup INT1 config failed\r\n");
    Error_Handler();
  }

  {
    int16_t ax_raw = 0;
    int16_t ay_raw = 0;
    int16_t az_raw = 0;
    uint8_t int1_level;
    uint8_t wake_source;
    char sensor_msg[128];

    LSM6DSR_Read_Accel(&ax_raw, &ay_raw, &az_raw);
    wake_source = LSM6DSR_Read_Wakeup_Source();
    int1_level = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_SET) ? 1U : 0U;
    snprintf(sensor_msg, sizeof(sensor_msg),
             "SENSOR_READY:lsm6dsr,who=0x%02X,int1=%u,wake=0x%02X,acc_mg=%ld,%ld,%ld\r\n",LSM6DSR_Read_ID(), int1_level,wake_source,(long)AccelRawToMg(ax_raw),(long)AccelRawToMg(ay_raw),(long)AccelRawToMg(az_raw));
    (void)ESP8266_SendAlert(sensor_msg);
  }
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN WHILE */
#if APP_NET_TEST_ONLY
    APP_NetworkTest_Run();
#else
    if (Sensor_Flag)
    {
      Sensor_Flag = 0;
      APP_HandleSensorWakeup();
    }
    else
    {
      APP_EnterStop2Mode();

      if (Sensor_Flag || (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_SET))
      {
        Sensor_Flag = 0;
        APP_HandleSensorWakeup();
      }
    }
#endif

    /* USER CODE END WHILE */
  }
  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                              | RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void SendMotionAlert(const MotionResult_t *result, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, uint8_t clear_wake, uint32_t delay_ms)
{
    char alert_msg[128] = {0};    // 网络警告消息缓冲区
    uint8_t send_ok = 0;          // 网络发送结果状态

//警告信息
    snprintf(alert_msg, sizeof(alert_msg),
             "警告,event=%s,time=%lu,acc_mg=%ld,%ld,%ld\r\n",
             result->type,
             (unsigned long)result->time_ms,
             (long)ax_mg,
             (long)ay_mg,
             (long)az_mg);
    ESP8266_Init(&huart5, (uint8_t *)gRX_Buff, 115200);
    send_ok = (uint8_t)ESP8266_SendAlert(alert_msg);
    if (send_ok == 0U)
    {
        printf("警告发送失败,event=%s,time=%lu,acc_mg=%ld,%ld,%ld\r\n",
               result->type,
               (unsigned long)result->time_ms,
               (long)ax_mg,
               (long)ay_mg,
               (long)az_mg);
    }

    PlayBeep(result->beep);

    if (clear_wake != 0U)
    {
        (void)LSM6DSR_Read_Wakeup_Source();
    }

    if (delay_ms > 0U)
    {
        HAL_Delay(delay_ms);
    }
}

void APP_HandleSensorWakeup(void) // 三层震动判断与告警处理
{
    MotionResult_t result = {0};  // 震动分类结果
    int32_t ax_mg = 0;            // 最后一组 X 轴加速度 mg 值
    int32_t ay_mg = 0;            
    int32_t az_mg = 0;          

    /*
     思路:
     1. 传感器中断唤醒后，连续采样一小段加速度序列；
     2. 计算相邻样本之间的变化峰值；
     3. 统计有效震动点数量和持续时间；
     4. touch1、touch2、touch3。
    */
    (void)LSM6DSR_Read_Wakeup_Source(); // 先读一次唤醒源，清掉锁存状态！！！！！！！！！！！！
    CheckMotion(&result, &ax_mg, &ay_mg, &az_mg);

    if (result.level == 0)
    {
        return;
    }

    if (result.send == 0)
    {
        printf("记录,event=%s,time=%lu,acc_mg=%ld,%ld,%ld\r\n",result.type,(unsigned long)result.time_ms,(long)ax_mg,(long)ay_mg,(long)az_mg);
        PlayBeep(result.beep);
        return;
    }

    SendMotionAlert(&result, ax_mg, ay_mg, az_mg, 1U, 0U);
}

static void CheckMotion(MotionResult_t *result, int32_t *ax_mg, int32_t *ay_mg, int32_t *az_mg)
{
    int16_t ax_raw = 0;           // X 轴原始值
    int16_t ay_raw = 0;           
    int16_t az_raw = 0;           
    int32_t last_ax = 0;          // 上一组 X 轴 mg 值
    int32_t last_ay = 0;          
    int32_t last_az = 0;         
    int32_t now_ax = 0;           // 当前 X 轴 mg 值
    int32_t now_ay = 0;           
    int32_t now_az = 0;           
    int32_t delta_x = 0;          // X 轴变化值
    int32_t delta_y = 0;         
    int32_t delta_z = 0;          
    int32_t delta_now = 0;        // 当前样本最大变化值
    int32_t peak = 0;             // 本次事件最大变化值
    uint8_t can_use_count = 0;    // 有效震动点数量
    uint32_t time_ms = 0;         // 事件持续时间

    result->type = "none";
    result->level = 0;
    result->peak = 0;
    result->can_use_count = 0;
    result->time_ms = 0;
    result->send = 0;
    result->beep = 0;

    // 阈值确定要找一个合适的基准值，后面的样本都和前一组做差。 （还没找到）
    LSM6DSR_Read_Accel(&ax_raw, &ay_raw, &az_raw);
    last_ax = AccelRawToMg(ax_raw);
    last_ay = AccelRawToMg(ay_raw);
    last_az = AccelRawToMg(az_raw);

    for (uint8_t i = 1; i < SMP_NUM; i++)
    {
        HAL_Delay(SMP_MS);
        LSM6DSR_Read_Accel(&ax_raw, &ay_raw, &az_raw);
        now_ax = AccelRawToMg(ax_raw);
        now_ay = AccelRawToMg(ay_raw);
        now_az = AccelRawToMg(az_raw);

        delta_x = APP_Abs32(now_ax - last_ax);
        delta_y = APP_Abs32(now_ay - last_ay);
        delta_z = APP_Abs32(now_az - last_az);
        delta_now = APP_Max3(delta_x, delta_y, delta_z);

        if (delta_now > peak)
        {
            peak = delta_now;
        }

        // 有效震动点要变化值达到 ACT_MG
        if (delta_now >= ACT_MG)
        {
            can_use_count++;
            time_ms = (uint32_t)i * SMP_MS;
        }

        last_ax = now_ax;
        last_ay = now_ay;
        last_az = now_az;
    }

    *ax_mg = last_ax;
    *ay_mg = last_ay;
    *az_mg = last_az;
    MarkMotion(result, peak, can_use_count, time_ms);
}

static void MarkMotion(MotionResult_t *result, int32_t peak, uint8_t can_use_count, uint32_t time_ms)
{
    result->peak = peak;
    result->can_use_count = can_use_count;
    result->time_ms = time_ms;
    result->beep = 0;

    if (peak < ACT_MG)
    {
        result->type = "none";
        result->level = 0;
        result->send = 0;
        result->beep = 0;
        return;
    }

    // touch1
    if ((peak < TOUCH_MG) && (can_use_count <= TOUCH_CNT) && (time_ms <= TOUCH_MS))
    {
        result->type = "touch1";
        result->level = 1;
        result->send = 0;
        result->beep = 1;
        return;
    }

    // touch2
    if ((peak < KNOCK_MG) && (can_use_count <= KNOCK_CNT) && (time_ms <= KNOCK_MS))
    {
        result->type = "touch2";
        result->level = 2;
        result->send = 1;
        result->beep = 2;
        return;
    }

    // touch3
    result->type = "touch3";
    result->level = 3;
    result->send = 1;
    result->beep = 3;
}

static int32_t APP_Abs32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static int32_t APP_Max3(int32_t a, int32_t b, int32_t c)
{
    int32_t max = a;

    if (b > max)
    {
        max = b;
    }

    if (c > max)
    {
        max = c;
    }

    return max;
}

static int32_t AccelRawToMg(int16_t raw)
{
    return ((int32_t)raw * 61) / 1000;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_5)
    {
        Sensor_Flag = 1;
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number,
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
