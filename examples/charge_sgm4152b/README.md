# 🔋 SGM41562B Charger Driver 🚀

## 📌 Overview
This project is a battery charging and monitoring application based on RT-Thread OS 🧠. 


## 🎯 Target Board
Designed for **T-Display-SF32** 🧩.

## ✨ Main Features
The code mainly does the following:
- ⚡ **Charger Control**: Initializes the SGM41562B IC and sets up charging parameters (400mA current, 4.2V voltage).
- 🔋 **Voltage Monitoring**: Reads the battery voltage via ADC.
- 🐶 **Watchdog Feeding**: Enables a 40s watchdog and feeds it regularly to keep the system running.
- 📊 **Status Logging**: Periodically prints the charging status and fault information to the console 🖥️.

## 📝 Build and Run
Please refer to the root directory.[Build and Run](../../readme.md#编译和烧录)
---
*Happy Hacking! 💻✨*