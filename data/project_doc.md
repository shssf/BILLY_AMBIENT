# **Circuit Documentation**

## **Summary**

This circuit is designed to integrate multiple sensors and LEDs with a FireBeetle ESP32 microcontroller. It includes motion sensors, a light-dependent resistor (LDR), and a series of WS2812B LEDs. The circuit is powered by a 5V power supply and utilizes a SN74AHCT125N buffer for signal conditioning. The ESP32 is used to read sensor data and control the LEDs.

## **Component List**

1. **FireBeetle ESP32**  
   * A versatile microcontroller with Wi-Fi and Bluetooth capabilities, used for processing sensor data and controlling outputs.  
2. **HC-SR505 Mini PIR Motion Sensing Module**  
   * A passive infrared sensor used to detect motion. Multiple units are used in this circuit.  
3. **SN74AHCT125N**  
   * A quad buffer/line driver with 3-state outputs, used for signal conditioning.  
4. **WS2812B LED**  
   * Individually addressable RGB LEDs, used for visual output.  
5. **KY-018 LDR Photo Resistor**  
   * A light-dependent resistor used to measure ambient light levels.  
6. **5V PSU**  
   * A power supply unit providing 5V DC to the circuit.  
7. **Resistor (325 Ohms)**  
   * Used to limit current in the circuit.

## **Wiring Details**

### **FireBeetle ESP32**

* **VCC** connected to **5V PSU V+**  
* **GND** connected to **5V PSU V-** and other ground connections  
* **D7/IO13** connected to **SN74AHCT125N 1A**  
* **A2/IO34** connected to **KY-018 LDR Photo Resistor Signal**  
* **LRCK/IO17** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **D4/IO27** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **MOSI/IO23** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **MISO/IO19** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **SCK/IO18** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **DI/IO16** connected to **HC-SR505 Mini PIR Motion Sensing Module out**  
* **3V3** connected to multiple **HC-SR505 Mini PIR Motion Sensing Module \+** and **KY-018 LDR Photo Resistor VCC**

### **HC-SR505 Mini PIR Motion Sensing Module**

* **\+** connected to **FireBeetle ESP32 3V3**  
* **out** connected to various **FireBeetle ESP32 pins** as described above  
* **\-** connected to **5V PSU V-**

### **SN74AHCT125N**

* **Vcc** connected to **5V PSU V+**  
* **GND** connected to **5V PSU V-**  
* **1A** connected to **FireBeetle ESP32 D7/IO13**  
* **1Y** connected to **Resistor pin1**  
* **1OE** connected to **5V PSU V-**

### **WS2812B LED**

* **\+5V** connected to **5V PSU V+**  
* **GND** connected to **5V PSU V-**  
* **Din** connected to **Resistor pin2**  
* **DO** connected to the next **WS2812B LED Din** in series

### **KY-018 LDR Photo Resistor**

* **Signal** connected to **FireBeetle ESP32 A2/IO34**  
* **VCC** connected to **FireBeetle ESP32 3V3**  
* **Ground** connected to **5V PSU V-**

### **5V PSU**

* **V+** connected to **FireBeetle ESP32 VCC**, **WS2812B LED \+5V**, and **SN74AHCT125N Vcc**  
* **V-** connected to all ground connections in the circuit

### **Resistor (325 Ohms)**

* **pin1** connected to **SN74AHCT125N 1Y**  
* **pin2** connected to **WS2812B LED Din**

## **Code**

There is no embedded code provided for this circuit. The FireBeetle ESP32 is expected to be programmed to read sensor inputs and control the WS2812B LEDs accordingly.