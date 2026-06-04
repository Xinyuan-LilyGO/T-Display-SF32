# LILYGO-T-SF32 SDK 更新安装

## 1. 下载SDK
下载最新的SDK，解压到任意目录，例如：`D:\SiFli-SDK`

## 2. Windows Terminal 快捷配置
查看[readme.md](./readme.md)文件，找到`Windows Terminal`的快捷配置,进行命令行的配置。

## 3.移植LILYGO-T-SF32相关硬件驱动库
### a. 移植板卡文件
将旧版本SDK文件的`customer\boards\t-display-sf32`和`customer\boards\t-display-sf32-base`文件夹复制到新SDK文件的`customer\boards`目录下。

修改新SDK文件`customer\boards\Kconfig_lcd`文件。
#### (1) 找到`BSP_USING_BUILTIN_LCD`的`choice`片段，在if语句里面添加以下内容：
```
    choice
        prompt "Built-in LCD module driver"
        default LCD_USING_ED_LB55DSI17801
    ......
        config LCD_USING_TFT_CO5300_T_SF32
                 bool "2.16 rect QSPI Single-Screen LCD(LCD_480*480_Lilygo_T_SF32_DevKit_Adapter_V1.0)"
                 select LCD_USING_CO5300
                 select TSC_USING_CST922 if BSP_USING_TOUCHD
                 select BSP_LCDC_USING_QADSPI
                 if LCD_USING_TFT_CO5300_T_SF32
                    config LCD_CO5300_VSYNC_ENABLE
                         bool "Enable LCD VSYNC (TE signal)"
                         def_bool y
                 endif
    endchoice

```

#### (2) 找到`BSP_USING_BUILTIN_LCD`的`LCD_HOR_RES_MAX`和`LCD_VER_RES_MAX`和`LCD_DPI`片段，添加以下内容：
```
config LCD_HOR_RES_MAX
        int
        ......
        default 480 if LCD_USING_TFT_CO5300_T_SF32

config LCD_VER_RES_MAX
        int
        ......
        default 480 if LCD_USING_TFT_CO5300_T_SF32

config LCD_DPI
        int
        ......
        default 314 if LCD_USING_TFT_CO5300_T_SF32

```
#### (3) 验证是否移植成功
打开`Windows Terminal`进入SDK终端,进入`T-Display-SF32\examples\empty_prj\project`文件夹，执行以下命令：
```
scons --board=t-display-sf32 --menuconfig  
scons --board=t-display-sf32_hcpu -j8  
```
如果出现menuconfig界面和编译通过，则表示移植成功。
![menuconfig](./res/image/menuconfig.png)
![build](./res/image/build.png)

### b. 移植触摸屏驱动库
将旧版本SDK文件的触摸驱动`customer\peripherals\cst922`复制到新SDK文件的`customer\peripherals`目录下。屏幕驱动使用`LCD_USING_CO5300`

修改新SDK文件`customer\peripherals\Kconfig`文件。
#### (1) 找到`BSP_USING_TOUCHD`片段，添加以下内容：
```
# TP driver of LCD module 
......

config TSC_USING_CST922
    bool
    default n
```

#### (2) 验证是否移植成功
打开`Windows Terminal`进入SDK终端,进入`T-Display-SF32\examples\lcd\project`文件夹，执行以下命令：
```
scons --board=t-display-sf32 --menuconfig 
```
进入menuconfig界面，选择`Config LCD on board -> Enable LCD on the board`，选择
![lcd_chiose](./res/image/lcd_chiose.png)
![lcd_meunconfig](./res/image/lcd_meunconfig.png)
按下`D`保存退出，执行以下编译命令和烧录命令：
```
scons --board=t-display-sf32_hcpu -j8  
build_t-display-sf32_hcpu\uart_download.bat
```
屏幕点亮，串口出现触摸坐标信息，代表移植成功。


### c. 移植硬件驱动
#### 音频控制
将旧版本SDK文件的硬件驱动`sifli-sdk\customer\peripherals\pa\NS4150B`复制到新SDK文件的`sifli-sdk\customer\peripherals\pa`目录下。
在`pa/Kconfig`文件中添加以下内容：
```
config PA_USING_NS4150B
    bool  "analog PA NS4150B enable"
    default n
    if PA_USING_NS4150B
        config NS4150B_GPIO_PIN
            int "NS4150B Control PIN"
            default 20
    endif 
```

#### 充电管理
将`sifli-sdk\external\charge`复制到`sifli-sdk\external`目录下。
#### GPS
将`sifli-sdk\external\gps`复制到`sifli-sdk\external`目录下。
#### KeyBorad
将`sifli-sdk\external\input_keyboard`复制到`sifli-sdk\external`目录下。
#### IO拓展
将`sifli-sdk\external\io_expand`复制到`sifli-sdk\external`目录下。
#### LORA
将`sifli-sdk\external\lora-radio-driver`复制到`sifli-sdk\external`目录下。
#### 传感器
将`sifli-sdk\external\sensor`复制到`sifli-sdk\external`目录下。

在`sifli-sdk\external\Kconfig`增加下面路径的配置：
```
source "$SIFLI_SDK/external/lora-radio-driver/Kconfig"
source "$SIFLI_SDK/external/charge/Kconfig"
source "$SIFLI_SDK/external/input_keyboard/Kconfig"
source "$SIFLI_SDK/external/io_expand/Kconfig"
source "$SIFLI_SDK/external/gps/Kconfig"
source "$SIFLI_SDK/external/sensor/Kconfig"
```
打开`Windows Terminal`进入SDK终端,进入`T-Display-SF32\examples\empty_prj\project`文件夹，执行以下命令：
```
scons --board=t-display-sf32 --menuconfig  
```
进入menuconfig界面,`->Third party packages`出现刚刚添加的硬件驱动选择项，则证明移植成功。
![third_menuconfig](./res/image/third_menuconfig.png)