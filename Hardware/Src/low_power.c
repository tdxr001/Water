#include "low_power.h"
#include "main.h"
#include "lsm6dsr.h"

extern void SystemClock_Config(void);

void APP_EnterStop2Mode(void)///低功耗
{
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_SET)
    {
        Sensor_Flag = 1;
        return;
    }
    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);  //保证中断唤醒
//唤醒后要配时钟
    SystemClock_Config();
    SystemClock_Config();
    HAL_ResumeTick();
}
