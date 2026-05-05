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
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_MOTION_POLL_DIAG 0
#define APP_MOTION_DELTA_MG 60
#define APP_MOTION_POLL_MS 200

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
static void APP_HandleMotionPollDiag(void);
static int32_t APP_Abs32(int32_t value);
static int32_t APP_Max3(int32_t a, int32_t b, int32_t c);
static int32_t APP_AccelRawToMg(int16_t raw);
static int32_t APP_GyroRawToDps(int16_t raw);
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
    snprintf(error_msg, sizeof(error_msg),
             "ERROR:LSM6DSR init failed,who_am_i=0x%02X,check wiring/address\r\n",
             LSM6DSR_Read_ID());
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
             "SENSOR_READY:lsm6dsr,who=0x%02X,int1=%u,wake=0x%02X,acc_mg=%ld,%ld,%ld\r\n",
             LSM6DSR_Read_ID(),
             int1_level,
             wake_source,
             (long)APP_AccelRawToMg(ax_raw),
             (long)APP_AccelRawToMg(ay_raw),
             (long)APP_AccelRawToMg(az_raw));
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
    /* 网络单项诊断模式：
       暂时不进入 STOP2，也不依赖 LSM6DSR。
       只验证底板 ESP8266 是否能连上 WiFi，并把数据发到电脑 TCP Server。 */
    APP_NetworkTest_Run();
#else
#if APP_MOTION_POLL_DIAG
    APP_HandleMotionPollDiag();
#else
    if (Sensor_Flag)
    {
      Sensor_Flag = 0;             //记得重置标志位
      APP_HandleSensorWakeup();
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
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
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
void APP_HandleSensorWakeup(void)
{
    int16_t ax_raw = 0;
    int16_t ay_raw = 0;
    int16_t az_raw = 0;
    int16_t gx_raw = 0;
    int16_t gy_raw = 0;
    int16_t gz_raw = 0;
    int32_t ax_mg,ay_mg,az_mg;
    int32_t gx_dps,gy_dps,gz_dps;
    int32_t gyro_peak_dps = 0;
    uint8_t wake_source;
    char alert_msg[220];


/*			思路（唤醒回来后）
读取 LSM6DSR 的 wake-up 来源;
读取加速度;
打开陀螺仪;
采几次陀螺仪数据;
算最大角速度;
	如果满足报警条件（阈值这里还不会）5-5，就通过 ESP8266 发 TCP 报警;
关闭陀螺仪;
*/
    
		
/* 
				LSM6DSR 的加速度唤醒中断已经说明传感器出现了一次明显动作。
				这里读取 WAKE_UP_SRC 清除传感器内部中断源（不清不进中断），再采一组加速度用于上报。
*/
    wake_source = LSM6DSR_Read_Wakeup_Source();
    LSM6DSR_Read_Accel(&ax_raw, &ay_raw, &az_raw);

    ax_mg = APP_AccelRawToMg(ax_raw);
    ay_mg = APP_AccelRawToMg(ay_raw);
    az_mg = APP_AccelRawToMg(az_raw);



    /* 被中断唤醒后短时间开启陀螺仪，采样几次判断是否有旋转/撬动。 */
    LSM6DSR_Enable_Gyro();
    HAL_Delay(20);
    for (uint8_t i = 0; i < LSM6DSR_GYRO_SAMPLE_COUNT; i++)
    {
        LSM6DSR_Read_Gyro(&gx_raw, &gy_raw, &gz_raw);
        gx_dps = APP_GyroRawToDps(gx_raw);
        gy_dps = APP_GyroRawToDps(gy_raw);
        gz_dps = APP_GyroRawToDps(gz_raw);
        gyro_peak_dps = APP_Max3(gyro_peak_dps,
                                 APP_Abs32(gx_dps),
                                 APP_Max3(APP_Abs32(gy_dps), APP_Abs32(gz_dps), 0));
        HAL_Delay(10);
    }
    LSM6DSR_Disable_Gyro();

    gx_dps = APP_GyroRawToDps(gx_raw);
    gy_dps = APP_GyroRawToDps(gy_raw);
    gz_dps = APP_GyroRawToDps(gz_raw);

    /* 告警条件分两层：（AI）
       1. LSM6DSR 自身已经触发 wake-up 中断，说明加速度扰动超过阈值；
       2. 陀螺仪峰值超过阈值，说明可能存在旋转、掀开或撬动动作。 */
    snprintf(alert_msg, sizeof(alert_msg),
             "ALERT:water_meter_tamper,source=int1,wake=0x%02X,acc_mg=%ld,%ld,%ld,gyro_dps=%ld,%ld,%ld,gyro_peak=%ld\r\n",
             wake_source,
             (long)ax_mg, (long)ay_mg, (long)az_mg,
             (long)gx_dps, (long)gy_dps, (long)gz_dps,
             (long)gyro_peak_dps);

    ESP8266_Init(&huart5, (uint8_t *)gRX_Buff, 115200);
    (void)ESP8266_SendAlert(alert_msg);

    (void)LSM6DSR_Read_Wakeup_Source();
}

static void APP_HandleMotionPollDiag(void)
{
    static uint8_t initialized = 0;
    static int32_t last_ax_mg = 0;
    static int32_t last_ay_mg = 0;
    static int32_t last_az_mg = 0;
    int16_t ax_raw = 0;
    int16_t ay_raw = 0;
    int16_t az_raw = 0;
    int32_t ax_mg;
    int32_t ay_mg;
    int32_t az_mg;
    int32_t delta_peak_mg;
    char alert_msg[160];

    LSM6DSR_Read_Accel(&ax_raw, &ay_raw, &az_raw);
    ax_mg = APP_AccelRawToMg(ax_raw);
    ay_mg = APP_AccelRawToMg(ay_raw);
    az_mg = APP_AccelRawToMg(az_raw);

    if (initialized == 0U)
    {
        initialized = 1U;
        last_ax_mg = ax_mg;
        last_ay_mg = ay_mg;
        last_az_mg = az_mg;
        HAL_Delay(APP_MOTION_POLL_MS);
        return;
    }

    delta_peak_mg = APP_Max3(APP_Abs32(ax_mg - last_ax_mg),
                             APP_Abs32(ay_mg - last_ay_mg),
                             APP_Abs32(az_mg - last_az_mg));

    last_ax_mg = ax_mg;
    last_ay_mg = ay_mg;
    last_az_mg = az_mg;

    if (delta_peak_mg >= APP_MOTION_DELTA_MG)
    {
        snprintf(alert_msg, sizeof(alert_msg),
                 "ALERT:water_meter_tamper,source=poll,acc_delta=%ld,acc_mg=%ld,%ld,%ld\r\n",
                 (long)delta_peak_mg,
                 (long)ax_mg, (long)ay_mg, (long)az_mg);

        ESP8266_Init(&huart5, (uint8_t *)gRX_Buff, 115200);
        (void)ESP8266_SendAlert(alert_msg);
        HAL_Delay(1200);
    }

    HAL_Delay(APP_MOTION_POLL_MS);
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

static int32_t APP_AccelRawToMg(int16_t raw)
{
    return ((int32_t)raw * 61L) / 1000L;
}

static int32_t APP_GyroRawToDps(int16_t raw)
{
    return ((int32_t)raw * 175L) / 10000L;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_5)
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
