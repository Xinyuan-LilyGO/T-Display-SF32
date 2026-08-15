# Hello World示例（RT-Thread）

## 支持的平台
<!-- 支持哪些板子和芯片平台 -->
- 任意板子（包括`pc`）

## 概述
Hello_world应用会在板子的串口打印hello world!


### 编译和烧录
切换到例程project目录，运行scons命令执行编译：
```
scons --board=t-display-sf32_hcpu -j8
```
运行`build_t-display-sf32_hcpu\uart_download.bat`，按提示选择端口即可进行下载：
```
build_t-display-sf32_hcpu\uart_download.bat

     Uart Download

please input the serial port num:5
```
关于编译、下载的详细步骤，请参考[quick_start](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/build.html)的相关介绍。


