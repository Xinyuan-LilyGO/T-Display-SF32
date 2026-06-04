# 调试问题
## 1. I2C找不到设备地址，可能设备需要复位


# 更新SDK，更改SDK代码记录
1、更新board:
> -- 创建 SiFli-SDK\customer\boards\t-display-sf32
> -- 创建 SiFli-SDK\customer\boards\t-display-sf32-base
> -- 创建 SiFli-SDK\customer\peripherals\co5300_t_sf32
> -- 创建 SiFli-SDK\customer\peripherals\cst922
> -- 修改 SiFli-SDK\customer\peripherals\Kconfig 增加屏幕和触摸菜单选择

2、SD卡
> -- spi_msd.c
> -- 修改SD卡SPI_CS片选，手动控制

3、LVGL 手势阈值调整：
> -- lv_indev.c
> -- #define LV_INDEV_DEF_GESTURE_LIMIT        240//50
> -- #define LV_INDEV_DEF_GESTURE_MIN_VELOCITY 8//3

