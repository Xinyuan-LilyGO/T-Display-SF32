# 📂 Project Overview
This project is based on the RT-Thread operating system and implements a user interface and hardware peripheral management system for a multi-functional smart device. Key features include: 
# 🧩 Core Functional Modules
User Interface System: Based on the LVGL graphics interface
Power Management: BQ21080 charge management
Storage System: SD card support
Audio Processing: Audio codec functionality
Wireless Communication: Bluetooth PAN network sharing implementation
Bluetooth Connection
LoRa Remote Communication
Sensor System: BHI260AP or ICM20948 IMU sensor
Input Device: TCA8418 keyboard controller
# ⚙️ Hardware Requirements
SF32LB52X microcontroller
AMOLED (2.16 inches) display:
Resolution: 240x240
IC: CO5300
TP IC: CST9220
Supported peripherals:
SD card interface
Bluetooth module
LoRa module
IMU sensor (BHI260AP or ICM20948)
TCA8418 keyboard controller
# ⚙️ Software Requirements
RT-Thread
LVGL Graphics Library (v9.2.0)
Hardware Driver Packages:
tca8418 (keyboard controller)
bq21080 (power management)
bhi260ap/icm20948 (IMU sensor)
LoRa_radio_driver (LoRa communication)
# 📝 Usage Instructions
#### Power On/Startup: 
Upon powering on, Bluetooth is enabled. For the first connection, find T-Display-SF32 on your phone and connect to it. If you need to use Web configuration, enable Bluetooth network sharing on your phone. If hardware initialization fails, an error window will pop up.
Main Interface:
After powering on, enter the main interface, where you can switch between menus using touch or buttons.
#### Music Interface: 
Bluetooth music player. Requires Bluetooth connection to support song selection, previous/next track, pause, play, and volume adjustment functions.
#### LoRa Interface: 
Send and receive LoRa data. You can change LoRa frequency, power, bandwidth, and other parameters.
#### Sensor Interface: 
View IMU sensor data, including Euler angles and other data.
#### Charging Interface: 
View battery level, charging status, charging voltage, and charging current data.
#### Timer Interface: 
Set a countdown timer and perform operations such as pause and restart.
#### Weather Interface: 
View weather and temperature data. Requires Bluetooth network sharing to obtain weather data.
#### Recording Interface: 
Record and play back audio recordings.
#### File Interface: 
View files on the SD card.
#### Keyboard Test Interface:
Test keyboard functionality.
System Interface: View system information and perform operations such as restarting and shutting down the device.

#### Button functions:
| Name | PIN   | Function                                  |
| ---- | ----- | ----------------------------------------- |
| A    | PIN34 | Power on                                  |
| B    | PIN33 | Screen on/off                             |
| C    | PIN35 | None                                      |
| D    | POWER | Power on/Enter ship mode (long press 5s+) |

#### Keyboard key mapping:
|     | 1            | 2         | 3        | 4           |
| --- | ------------ | --------- | -------- | ----------- |
| 4   | KEY_PREV(⬅)  |           | KEY_HOME |             |
| 3   | KEY_ENTER(⏹) | KEY_UP(⬆) |          | KEY_DOWN(⬇) |
| 2   | KEY_NEXT(➡)  |           |          |             |
| 1   |              |           |          |             |
| 0   |              |           |          |             |

```c
KEY_PREV: Select the previous item
KEY_ENTER: Confirm
KEY_NEXT: Select the next item
KEY_HOME: Main interface
KEY_UP: For list elements, select the previous item
KEY_DOWN: For list elements, select the next item
```

# 📝 Compilation and Flashing
##### To compile and flash the configuration, navigate to the project directory in the current directory and execute the following commands: 
    1. scons --board=t-display-sf32_hcpu -j8 - Compile for hcpu
    2. build_t-display-sf32_hcpu\uart_download.bat - Flash for hcpu
    3. scons --board=t-display-sf32 --menuconfig - Configure menu
##### For instructions on how to install the environment, refer to the readme file in the root directory.