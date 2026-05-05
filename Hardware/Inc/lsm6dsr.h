#ifndef __LSM6DSR_H
#define __LSM6DSR_H

#include "main.h"

extern volatile uint8_t Sensor_Flag;

#define LSM6DSR_I2C_address             0xD6  // LSM6DSR I2C 8位写地址
#define LSM6DSR_WHO_AM_I                0x0F  // 器件 ID 寄存器，LSM6DSR 固定返回 0x6B
#define LSM6DSR_CTRL1_XL                0x10  // 加速度计控制寄存器，配置 ODR/量程/滤波
#define LSM6DSR_CTRL2_G                 0x11  // 陀螺仪控制寄存器，配置 ODR/量程
#define LSM6DSR_CTRL3_C                 0x12  // 通用控制寄存器，配置 BDU、地址自增等
#define LSM6DSR_WAKE_UP_SRC             0x1B  // 唤醒源状态寄存器，读取可确认/清除 wake-up 事件
#define LSM6DSR_OUTX_L_G                0x22  // 陀螺仪 X 轴低字节输出寄存器起始地址
#define LSM6DSR_OUTX_L_A                0x28  // 加速度计 X 轴低字节输出寄存器起始地址
#define LSM6DSR_TAP_CFG0                0x56  // Tap/wake 相关配置寄存器 0；																好像还能敲击检测
#define LSM6DSR_TAP_CFG2                0x58  // Tap/wake 中断使能相关配置寄存器 2；												还管中断；唤醒
#define LSM6DSR_WAKE_UP_THS             0x5B  // 唤醒阈值配置寄存器
#define LSM6DSR_WAKE_UP_DUR             0x5C  // 唤醒持续时间/睡眠持续时间配置寄存器
#define LSM6DSR_MD1_CFG                 0x5E  // INT1 中断路由配置寄存器

/*
 * Keep the register map explicit even if an editor/encoding joins a comment
 * with the next #define.  The values below are the ones used by the driver.
 */
#undef LSM6DSR_CTRL3_C
#undef LSM6DSR_WAKE_UP_SRC
#undef LSM6DSR_TAP_CFG0
#undef LSM6DSR_TAP_CFG2
#undef LSM6DSR_WAKE_UP_THS
#undef LSM6DSR_WAKE_UP_DUR
#undef LSM6DSR_MD1_CFG
#define LSM6DSR_CTRL3_C                 0x12
#define LSM6DSR_WAKE_UP_SRC             0x1B
#define LSM6DSR_TAP_CFG0                0x56
#define LSM6DSR_TAP_CFG2                0x58
#define LSM6DSR_WAKE_UP_THS             0x5B
#define LSM6DSR_WAKE_UP_DUR             0x5C
#define LSM6DSR_MD1_CFG                 0x5E

/* With WAKE_THS_W=0 and FS_XL=+/-2g, wake-up threshold LSB is about 31.25mg. */
#define LSM6DSR_WAKEUP_THS_62MG         0x02
#define LSM6DSR_ACCEL_ALERT_MG          350
#define LSM6DSR_GYRO_ALERT_DPS          120
#define LSM6DSR_GYRO_SAMPLE_COUNT       4

uint8_t LSM6DSR_Init(void);
uint8_t LSM6DSR_Read_ID(void);
uint8_t LSM6DSR_Config_Wakeup_INT1(void);
void LSM6DSR_Enable_Gyro(void);
void LSM6DSR_Disable_Gyro(void);
void LSM6DSR_Read_Accel(int16_t *Acc_x, int16_t *Acc_y, int16_t *Acc_z);
void LSM6DSR_Read_Gyro(int16_t *Gyro_x, int16_t *Gyro_y, int16_t *Gyro_z);
uint8_t LSM6DSR_Read_Wakeup_Source(void);

#endif
