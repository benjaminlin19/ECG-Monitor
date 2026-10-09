# 🫀 ECG Monitor

A portable electrocardiogram (ECG) monitor built using an Arduino. This project captures analog cardiac electrical activity, processes the signal, and displays the real-time heart data.

## 🛠️ Hardware Requirements
* **Microcontroller:** ESP8266
* **ECG Sensor Module:** AD8232 Heart Rate Monitor
* **Electrodes:** 3-lead ECG biomedical sensor pads
* **Display (Optional):** OLED Display or Serial Plotter

## 🔌 Pin Connections (AD8232 to Arduino)
* **GND** ➡️ GND
* **3.3V** ➡️ 3.3V
* **OUTPUT** ➡️ Analog Pin A0
* **LO-** ➡️ Digital Pin 11
* **LO+** ➡️ Digital Pin 10

## 🚀 How to Use
1. Clone this repository to your machine.
2. Open `ECG-Monitor.ino` in the Arduino IDE.
3. Connect your Arduino board via USB.
4. Select your correct **Board** and **Port** under the Tools menu.
5. Upload the code and open the **Serial Plotter** (`Ctrl + Shift + L` or `Cmd + Shift + L` on Mac) set to **9600 baud** to visualize your heartbeat.
