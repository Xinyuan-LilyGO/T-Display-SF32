<h1 align = “center”> T-SF32-Display </h1>


# SDK
Click the link below to download the SDK and place it in your desired path: [https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo](https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo)
To update the SDK version, please refer to the update_sdk_install.md document: [https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo/blob/master/update_sdk_install.md](https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo/blob/master/update_sdk_install.md)

# Overview
The T-SF32-Display development board is a platform based on SiFli's latest ultra-low-power AIoT MCU chip SF32LB52X. It is designed for smart wearables, smart home, industrial sensing, and IoT applications, featuring a rich set of peripheral interfaces and sensors.

# Hardware Features
## 1. MCU
|                    | SF32LB52X                                            |
| ------------------ | ---------------------------------------------------- |
| Model              | SF32LB52X                                            |
| Processor          | Arm Cortex-M33 STAR-MC1 big.LITTLE architecture      |
| Big Core (HCPU)    | 192MHz, 787 CoreMark                                 |
| Little Core (LCPU) | 24MHz                                                |
| Memory             | 576KB SRAM (512KB+64KB)                              |
| Bluetooth          | Dual-mode Bluetooth 5.3 (BLE 5.3, Classic Bluetooth) |
| Graphics Engine    | ePicassoTM 2.5D high-performance graphics engine     |
| Operating Voltage  | 3.2V-4.7V                                            |

## 2. Peripheral Modules
| Module            | Model             | Description                                                           |
| ----------------- | ----------------- | --------------------------------------------------------------------- |
| Bluetooth         | -                 | Dual-mode Bluetooth 5.3, BLE Audio, RX sensitivity -100dBm (1Mbps)    |
| Audio             | -                 | 24-bit audio ADC/DAC, Bluetooth audio streaming                       |
| LoRa              | SX1262            | 433/868/915MHz, low power, high RX sensitivity                        |
| TF Card           | MicroSD           | Supports SDHC/SDXC                                                    |
| IMU               | BHI260AP          | 3-axis accelerometer, gyroscope, magnetometer, low-power mode         |
| Charge Management | SGM41562B         | USB PD fast charging, battery management, multiple charging protocols |
| Keyboard          | TAC8418 + AW21009 | 8x8 matrix keyboard, low-power design                                 |
| GPS               | L76K              | Supports NMEA, CASIC protocols, high-precision positioning            |
| Temp & Humidity   | BME280            |                                                                       |
| IR Transmitter    | VSMY14940         |                                                                       |
| Vibration Motor   | AW86224           |                                                                       |

## 3. Storage
| Module | Model |
| ------ | ----- |
| Flash  | 16MB  |
| PSRAM  | 8MB   |

## 4. Display
| Module        | Model    |
| ------------- | -------- |
| AMOLED(2.16”) | (CO5300) |
| Touch Screen  | CST9220  |

## 5. Interfaces
| Interface | Type    | Description |
| --------- | ------- | ----------- |
| USB       | Type-C  |             |
| Serial    | UART    |             |
| I2C       | 4 ch    |             |
| SPI       | 2 ch    |             |
| GPIO      | 45 pins |             |
| Audio     | 3.5mm   |             |
| JTAG/SWD  | -       |             |

## T-SF32-Display Power Consumption
Please refer to the [Power Consumption Test Report](./test/T-SF32-Display-power.pdf), located at “./test/T-SF32-Display-power.pdf”.

# Environment Setup
## Windows
Please refer to [T-SF32-Display SIFLI-SDK Windows Installation](https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo/blob/master/readme.md)

## Linux and macOS
Please refer to [SIFLI-SDK macOS and Linux Installation](https://docs.sifli.com/projects/sdk/v2.4/sf32lb52x/quickstart/install/script/unix.html)

# Build and Flash
1. First, install the required dependencies and configure environment variables as per the installation guide (the following commands are executed in PowerShell):
```powershell
    cd SIFLI\T-Display-SF32\examples\rt_os\rt_driver\project  # Enter project directory
    scons --board=t-display-sf32_hcpu -j8   # Build
    build_t-display-sf32_hcpu\uart_download.bat     # Flash
```
2. Wait for the build to complete, then run the `build_t-display-sf32_hcpu\uart_download.bat` command and enter the device port number to flash the firmware.
3. Example screenshots:
![Build](./image/build1.png)
![Build](./image/bulid.png)
![Build](./image/build3.png)

# Official Documentation
This project references examples from the official website. For detailed documentation, please refer to the following links:
[SIFLI-SDK](https://docs.sifli.com/projects/sdk/v2.4/sf32lb52x/index.html)
[SIFLI-WIKI](https://wiki.sifli.com/)
[RT-Thread](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/README)

# FAQ

#### 1. Why can't I use Bluetooth even after configuring Bluetooth-related macros in menuconfig?
Possible reasons:
1. The LCPU compilation files are not added in SConscript and SConstruct. The `lcpu_general_ble_img` directory contains the default LCPU code including BLE initialization. For users who only need basic BLE functionality, you can add `lcpu_img.c` to your HCPU project and refer to the BLE examples for usage.
```c
SConscript:
    objs.extend(SConscript(os.path.join(SIFLI_SDK, 'example/rom_bin/lcpu_general_ble_img/SConscript'), variant_dir=”lcpu_patch”, duplicate=0))

SConstruct:
    AddLCPU(SIFLI_SDK,rtconfig.CHIP,”../../src/lcpu_img.c”)
```

#### 2. Why does Impeller flashing keep failing?
Possible reasons:
1. Impeller flashing requires a USB Type-C connection. Make sure you are using the correct interface.
2. Ensure the device port number is correct. You can check it in Device Manager.
3. Ensure the device is properly connected. You can verify the connection in Device Manager.
4. Ensure the correct driver is installed. The driver file is located at `tools/VisualCppRedist_54_Setup.7z`, [C++ Driver](./tools/VisualCppRedist_54_Setup.7z).

#### 3. Why can't the device power on via USB charging after shutting down with the factory firmware (menu_app)?
The factory firmware (menu_app) enters low-power mode after shutdown. In low-power mode, only a button press can wake the device — USB charging cannot wake it. However, the device can still charge normally while powered off; it just cannot be turned on via USB connection alone.
