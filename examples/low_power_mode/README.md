# 低功耗模式示例（RT-Thread）

## 支持的平台

- T-Display-SF32（SF32LB52X）

## 概述

本例程演示 SF32LB52X 芯片的 4 级低功耗模式：**Deepsleep**、**Standby** 和 **Hibernate**（冬眠），从浅到深功耗依次降低，唤醒延迟依次增大。
sflb52x系列建议关掉上面standby，改用deep休眠

## menuconfig宏定义
```
#define RT_USING_PM 1
#define BSP_USING_PM 1 //开启低功耗模式
//#define PM_STANDBY_ENABLE 1 //进入standby模式的低功耗,
#define PM_DEEP_ENABLE 1  //52系列建议关掉上面standby，改用deep休眠
#define BSP_PM_DEBUG 1 //打开低功耗模式调试log
```

## 运行逻辑

1. 增大 LXT 低频晶振驱动电流，提高时钟稳定性
2. 使能 LDO（PA41），给外部外设供电
3. 调用 `rt_pm_release(PM_SLEEP_MODE_IDLE)` 通知 RT-Thread PM 框架允许自动进入 Light Sleep —— 此后 5 秒内，系统空闲时会自动浅睡
4. 配置 PA34 为 GPIO 上升沿唤醒源
5. 检测启动原因：若从 Hibernate 唤醒则打印提示
6. 主循环：每 5 秒通过定时器唤醒打印 `hcpu timer wakeup!!!`

## 四种低功耗模式

| 模式      | CPU  | SRAM         | 唤醒源        | 唤醒延迟 |
| --------- | ---- | ------------ | ------------- | -------- |
| Deepsleep | 停   | 全保留       | RTC/PIN/LPTIM | ~250µs   |
| Standby   | 复位 | 384KB 保留区 | RTC/PIN/LPTIM | ~1ms     |
| Hibernate | 断电 | 丢失         | RTC/PIN       | >2ms     |

- **Deepsleep**：手动关高速时钟、HPSYS deactivate 后 WFI，唤醒后恢复时钟和中断继续执行
- **Standby**：CPU 会复位，低 384KB SRAM 保留，唤醒走 ROM boot 恢复流程
- **Hibernate**：全系统断电，仅 PMU/RTC/IWDT 存活，唤醒后系统完全重启

![](./image/power_pm_deepsleep.png)
![](./image/power_pm_deepsleep_on.png)
![](./image/power_hibernate.png)


## 编译和烧录

切换到例程 project 目录，运行 scons 命令执行编译：

```
scons --board=t-display-sf32_hcpu -j8
```

运行 `build_t-display-sf32_hcpu\uart_download.bat`，按提示选择端口即可进行下载：

```
build_t-display-sf32_hcpu\uart_download.bat

     Uart Download

please input the serial port num:5
```

关于编译、下载的详细步骤，请参考 [quick_start](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/build.html) 的相关介绍。
