# 🛠️ Hob2Hood ESP32 Control System

Image: ESP32 DevKitC board used in this project (placeholder)

📌 What is Hob2Hood?
Hob2Hood is an automation system that allows an induction hob (cooktop) to communicate wirelessly with a kitchen range hood. It controls the ventilation fan speed and lighting based on the hob’s state — for example, turning on the fan when cooking starts, or adjusting its speed as the heat increases.

Some hobs and hoods support Hob2Hood natively (e.g. AEG, Electrolux), using infrared (IR) communication to transmit commands.
This project replicates and enhances that functionality using an ESP32, allowing:
Manual or IR-based fan/light control
OTA (Over-The-Air) firmware updates
Web-based control interface
Integration with other smart systems if extended

🚀 What This Program Does
This firmware running on the ESP32:
Listens to IR commands using the RMT peripheral
Decodes NEC-format signals
Maps decoded signals to known Hob2Hood commands (Vent1–4, Light On/Off)
Controls output GPIO pins that switch relays or logic-level drivers connected to:
Fan speed relays (Vent1–4)
Light switch
Hosts a web interface with buttons for manual control (via SerialHTML)
Allows real-time command reception over WebSocket
Supports OTA updates, with IR and SerialHTML safely paused during update
Implements a watchdog timer for reliability

🔌 Wiring / Pinout (ESP32 to Hood)
Function	ESP32 GPIO	Notes lll
IR Receiver	GPIO 5	Connect IR sensor OUT pin here
Fan Speed (1)	GPIO 32	Relay or level shifter
Fan Speed (2/3)	GPIO 27	Shared relay for levels 2 & 3
Fan On/Off Ctrl	GPIO 19	Main ventilation power
Light On/Off	GPIO 25	Relay to control hood light
Built-in LED	GPIO 2	Optional status indicator

All output pins drive relays or transistors that emulate button presses or control hood power rails.

🌐 Web Interface (SerialHTML)
Simple control page served by ESP32, with buttons to control Light and Vent speed via WebSocket.

🔧 Installation & Setup\
Ensure you have the following libraries installed:\

ESPAsyncWebServer\
AsyncTCP\
ArduinoOTA\
SerialHTML (custom or internal library)\
Connect to the ESP32's IP address shown in serial output after WiFi connection.\

⚙️ Special Features\
🔧 Modular structure: clean, well-organized functions.\
📡 OTA integrated safely to enable over the air updates (disables IR for robustness).\
🔄 Watchdog Timer (WDT) to ensure recovery from freezing/lockups.\
🌐 WiFi reconnect logic is lightweight and correct.\
📲 SerialHTML abstraction for WebSerial with realtime WebSocket control.\
📦 CommandQueue + xQueue usage is perfect for decoupling.\
🕹️ IR Task on separate core giving robustness and responsiveness.\

🧠 IR Code Mapping\
IR Code (Hex)	Function\
0xE208293C	Light ON\
0x24ACF947	Light OFF\
0xE3C01BE2	Ventilation 1\
0xD051C301	Ventilation 2\
0xC22FFFD7	Ventilation 3\
0xB9121B29	Ventilation 4\
0x055303A3	Ventilation OFF\



🧪 Debugging & Logs\
Open the Web Serial Monitor at ESP32Hob2Hood/serial\
IR signal decoding\
Fan/light command handling\
OTA state\
WiFi reconnection attempts\
Watchdog resets (if triggered)\
📸 Images
 Schematic / wiring diagram coming\
 ![Schematic](Hob2Hood.jpg?raw=true "Hob2Hood")
 ESP32 board photo installed in your hood\
 Screenshot of the web control page\
