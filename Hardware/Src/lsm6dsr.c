#include "lsm6dsr.h"
#include "i2c.h"

volatile uint8_t Sensor_Flag = 0;

static HAL_StatusTypeDef LSM6DSR_WriteReg(uint8_t reg, uint8_t data)
{
    return HAL_I2C_Mem_Write(&hi2c1, LSM6DSR_I2C_address, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
}

static HAL_StatusTypeDef LSM6DSR_ReadReg(uint8_t reg, uint8_t *data)
{
    return HAL_I2C_Mem_Read(&hi2c1, LSM6DSR_I2C_address, reg, I2C_MEMADD_SIZE_8BIT, data, 1, 1000);
}

uint8_t LSM6DSR_Init(void)
{
    uint8_t check_id = 0;

    if (LSM6DSR_ReadReg(LSM6DSR_WHO_AM_I, &check_id) != HAL_OK)
    {
        return 0;
    }

    if (check_id != 0x6B)
    {
        return 0;
    }
    /* CTRL3_C:
       bit6 BDU=1，读高低字节时锁存数据，避免读到一半数据刷新；
       bit2 IF_INC=1，连续读取 OUTX/OUTY/OUTZ 时寄存器地址自动递增。 */
    if (LSM6DSR_WriteReg(LSM6DSR_CTRL3_C, 0x44) != HAL_OK)
    {
        return 0;
    }

    /* 加速度计保持常开，用 104Hz / +/-2g。
       STM32 休眠时由 LSM6DSR 自己判断 wake-up，中断经 INT1 输出到 PA5。 */
    if (LSM6DSR_WriteReg(LSM6DSR_CTRL1_XL, 0x60) != HAL_OK)
    {
        return 0;
    }

    LSM6DSR_Disable_Gyro();
    return 1;
}

uint8_t LSM6DSR_Read_ID(void)
{
    uint8_t check_id = 0;

    if (LSM6DSR_ReadReg(LSM6DSR_WHO_AM_I, &check_id) != HAL_OK)
    {
        return 0;
    }

    return check_id;
}

uint8_t LSM6DSR_Config_Wakeup_INT1(void)
{
    if (LSM6DSR_WriteReg(LSM6DSR_TAP_CFG0, 0x50) != HAL_OK)
    {
        return 0;
    }

    /* 打开 LSM6DSR 内部中断 */
    if (LSM6DSR_WriteReg(LSM6DSR_TAP_CFG2, 0x80) != HAL_OK)
    {
        return 0;
    }

    /* 设置加速度唤醒阈值，默认约 125mg。
       现场如果误报多，可以先提高这个宏；如果不够灵敏，则降低这个宏。 */
    if (LSM6DSR_WriteReg(LSM6DSR_WAKE_UP_THS, LSM6DSR_WAKEUP_THS_62MG) != HAL_OK)
    {
        return 0;
    }

    /* 0 表示不增加额外持续时间要求，适合先把功能调通；
       后续如果需要过滤瞬时冲击，可以把 WAKE_UP_DUR 调大。 */
    if (LSM6DSR_WriteReg(LSM6DSR_WAKE_UP_DUR, 0x00) != HAL_OK)
    {
        return 0;
    }

    /* MD1_CFG bit5(INT1_WU)=1：把 wake-up 事件输出到 INT1。
       本项目中 INT1 接 STM32 PA5，PA5 已经配置为 EXTI 上升沿唤醒。 */
    (void)LSM6DSR_Read_Wakeup_Source();
    HAL_Delay(20);

    if (LSM6DSR_WriteReg(LSM6DSR_MD1_CFG, 0x20) != HAL_OK)
    {
        return 0;
    }

    return 1;
}

void LSM6DSR_Enable_Gyro(void)
{
    (void)LSM6DSR_WriteReg(LSM6DSR_CTRL2_G, 0x44);
}

void LSM6DSR_Disable_Gyro(void)
{
    (void)LSM6DSR_WriteReg(LSM6DSR_CTRL2_G, 0x00);
}

void LSM6DSR_Read_Accel(int16_t *Acc_x, int16_t *Acc_y, int16_t *Acc_z)               //江科大10-3-40左右
{
    uint8_t raw_data[6] = {0};

    (void)HAL_I2C_Mem_Read(&hi2c1, LSM6DSR_I2C_address, LSM6DSR_OUTX_L_A,
                           I2C_MEMADD_SIZE_8BIT, raw_data, 6, 1000);

    *Acc_x = (int16_t)((raw_data[1] << 8) | raw_data[0]);
    *Acc_y = (int16_t)((raw_data[3] << 8) | raw_data[2]);
    *Acc_z = (int16_t)((raw_data[5] << 8) | raw_data[4]);
}

void LSM6DSR_Read_Gyro(int16_t *Gyro_x, int16_t *Gyro_y, int16_t *Gyro_z)
{
    uint8_t raw_data[6] = {0};

    (void)HAL_I2C_Mem_Read(&hi2c1, LSM6DSR_I2C_address, LSM6DSR_OUTX_L_G,
                           I2C_MEMADD_SIZE_8BIT, raw_data, 6, 1000);

    *Gyro_x = (int16_t)((raw_data[1] << 8) | raw_data[0]);
    *Gyro_y = (int16_t)((raw_data[3] << 8) | raw_data[2]);
    *Gyro_z = (int16_t)((raw_data[5] << 8) | raw_data[4]);
}

uint8_t LSM6DSR_Read_Wakeup_Source(void)
{
    uint8_t source = 0;

    (void)LSM6DSR_ReadReg(LSM6DSR_WAKE_UP_SRC, &source);
    return source;
}
