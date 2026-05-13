#include "beep.h"

#include "gpio.h"

static void BeepOn(void);  // 拉高 PA15
static void BeepOff(void); // 拉低 PA15

void PlayBeep(uint8_t beep)
{

    if (beep == 1)
    {
        for (uint8_t i = 0; i < 4; i++)
        {
            BeepOn();
            HAL_Delay(250);
            BeepOff();
            HAL_Delay(250);
        }
        return;
    }

    if (beep == 2)
    {
        for (uint8_t i = 0; i < 10; i++)
        {
            BeepOn();
            HAL_Delay(100);
            BeepOff();
            HAL_Delay(200);
        }
        return;
    }

    if (beep == 3)
    {
        for (uint8_t i = 0; i < 50; i++)
        {
            BeepOn();
            HAL_Delay(50);
            BeepOff();
            HAL_Delay(50);
        }
        return;
    }

    BeepOff();
}

static void BeepOn(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
}

static void BeepOff(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
}
