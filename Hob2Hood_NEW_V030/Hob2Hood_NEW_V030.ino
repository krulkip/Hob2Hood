/*
Access to SerialHTML through WiFi.localIP/serial
As mDNS is activated you can also call ESP32Hob2Hood/serial
This is especially usefull if there is no serial connection available
Commands through SerialHTML Vent1-Vent4 Vent off Light on Light off
Hardware TSOP4838 any 38kHz IR Receiver
                 A      ON/OFF        B
     LOW    -----o         /o       o
                   /o----o/           /o---- OUT
     HIGH   -----o/         o-------o/
             A       B
NC = normally open = OFF --> relay activated for ON
   A   B   Situation
   0   1   0  Vent off
   1   1   1  Vent 1  low
   1   1   1  Vent 2  low
   0   1   1  Vent 3  high
   0   1   1  Vent 4  high not used Vent 4 mapped to Vent 3
Switch LOW/HIGH with A, while B and ON/OFF switch must be ON
On my Hood only 2 ventilator levels LOW and HIGH mapped to --> Vent1 = Vent4
If you want the inbuilt LED to not show incoming IR signals change 
ENABLE_LED_FEEDBACK to DISABLE_LED_FEEBBACK
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SerialHTML.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#define DECODE_HASH
#include <IRremote.hpp>
#include "secret.h"
#include "esp_task_wdt.h"  // ⬅️ Added for Watchdog

const int PIN_OUT_VENT_B = 27;
const int PIN_OUT_VENT_A = 32;
const int PIN_OUT_VENT_ONOFF = 19;
const int PIN_OUT_LIGHT = 25;

#define IR_RECEIVE_PIN 5
#define LED_BUILTIN 2

#define WDT_TIMEOUT 5  // ⬅️ Timeout in seconds

const long IRCMD_VENT_1   = 0xE3C01BE2;
const long IRCMD_VENT_2   = 0xD051C301;
const long IRCMD_VENT_3   = 0xC22FFFD7;
const long IRCMD_VENT_4   = 0xB9121B29;
const long IRCMD_VENT_OFF = 0x055303A3;
const long IRCMD_LIGHT_ON = 0xE208293C;
const long IRCMD_LIGHT_OFF= 0x24ACF947;

#define OFF HIGH
#define ON LOW

enum VentState { VENT_OFF, VENT_LOW, VENT_HIGH };
VentState currentVentState = VENT_OFF;
volatile unsigned long lastCommandTime = 0;

AsyncWebServer server(80);
QueueHandle_t commandQueue;

unsigned long previousMillis = 0;
const long interval = 1000;
int ledState = LOW;
int loopCounter = 0;
long lastRawData = 0;
unsigned long lastIRTime = 0;

volatile bool serialClientConnected = false;
#define SAFE_PRINTLN(msg) do { if (serialClientConnected) SerialHTML.println(msg); } while(0)
#define SAFE_PRINTF(...)  do { if (serialClientConnected) SerialHTML.printf(__VA_ARGS__); } while(0)

// --- WDT Helpers ---
void enableWDT() {
  const esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,  // Convert seconds to ms
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // Watch all cores
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);  // Add current task to WDT
}

void disableWDT() {
  esp_task_wdt_delete(NULL);
  Serial.println("WDT disabled");
}
// --------------------

void handleCommand(const String& cmdRaw) {
  String cmd = cmdRaw;
  cmd.trim();
  cmd.toUpperCase();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  SAFE_PRINTF("CMD: '%s'\n", cmd.c_str());
  vTaskDelay(100 / portTICK_PERIOD_MS);
  static unsigned long lastPrintTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime < 100) {
    vTaskDelay(100 - (currentTime - lastPrintTime) / portTICK_PERIOD_MS);
  }
  lastPrintTime = millis();

  if (cmd == "LIGHT ON") {
    digitalWrite(PIN_OUT_LIGHT, ON);
  } else if (cmd == "LIGHT OFF") {
    digitalWrite(PIN_OUT_LIGHT, OFF);
  } else if (cmd == "VENT1" || cmd == "VENT2") {
    if (currentVentState != VENT_LOW) {
      digitalWrite(PIN_OUT_VENT_A, ON);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(PIN_OUT_VENT_ONOFF, ON);
      currentVentState = VENT_LOW;
    }
  } else if (cmd == "VENT3" || cmd == "VENT4") {
    if (currentVentState == VENT_OFF) {
      digitalWrite(PIN_OUT_VENT_A, ON);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(PIN_OUT_VENT_ONOFF, ON);
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    if (currentVentState != VENT_HIGH) {
      digitalWrite(PIN_OUT_VENT_A, OFF);
      currentVentState = VENT_HIGH;
    }
  } else if (cmd == "VENT OFF") {
    if (currentVentState != VENT_OFF) {
       digitalWrite(PIN_OUT_VENT_A, ON);
       vTaskDelay(100 / portTICK_PERIOD_MS);
       digitalWrite(PIN_OUT_VENT_ONOFF, OFF);
       currentVentState = VENT_OFF;
    }   
  } else if (cmd == "PING") {
  } else if (cmd == "RESET") {
    ESP.restart();
  } else {
    SAFE_PRINTLN("CMD: Unknown command");
  }
  vTaskDelay(200 / portTICK_PERIOD_MS);
  lastCommandTime = millis();
}

void receiveMessage(AsyncWebSocketClient* client, uint8_t* data, size_t length) {
  String message;
  for (size_t i = 0; i < length; i++) {
    message += char(data[i]);
  }
  SAFE_PRINTF("SerialHTML Received: %s\n", message.c_str());
  if (commandQueue != NULL) {
    xQueueSend(commandQueue, &message, portMAX_DELAY);
  }
}

void connectRequest(AsyncWebSocketClient* client) {
  serialClientConnected = true;
  client->text("Connection request received...\n");
}

void disconnectRequest(AsyncWebSocketClient* client) {
  serialClientConnected = false;
}

void IRTask(void* parameter) {
  esp_task_wdt_add(NULL);  // Add this task to WDT too
  while (true) {
    if (IrReceiver.decode()) {
      long rawData = IrReceiver.decodedIRData.decodedRawData;
      IrReceiver.resume();

      if (rawData == 0) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      unsigned long now = millis();
      if (rawData != lastRawData && now - lastIRTime > 200 && now - lastCommandTime > 300) {
        lastIRTime = millis();
        SAFE_PRINTF("Received IR: %lX\n", rawData);
        lastRawData = rawData;
      } else {
        continue;
      }

      String cmd;
      switch (rawData) {
        case IRCMD_LIGHT_ON: cmd = "LIGHT ON"; break;
        case IRCMD_LIGHT_OFF: cmd = "LIGHT OFF"; break;
        case IRCMD_VENT_1: cmd = "VENT1"; break;
        case IRCMD_VENT_2: cmd = "VENT2"; break;
        case IRCMD_VENT_3:
        case IRCMD_VENT_4: cmd = "VENT3"; break;
        case IRCMD_VENT_OFF: cmd = "VENT OFF"; break;
        default:
          SAFE_PRINTF("IR: Unknown code 0x%lX\n", rawData);
          break;
      }

      if (cmd.length() > 0 && commandQueue != NULL) {
        xQueueSend(commandQueue, &cmd, portMAX_DELAY);
      }
    }
    esp_task_wdt_reset();   // Feed WDT inside this task
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_OUT_VENT_B, OUTPUT);
  pinMode(PIN_OUT_VENT_A, OUTPUT);
  pinMode(PIN_OUT_VENT_ONOFF, OUTPUT);
  pinMode(PIN_OUT_LIGHT, OUTPUT);

  digitalWrite(PIN_OUT_VENT_B, ON);
  digitalWrite(PIN_OUT_VENT_A, ON);
  digitalWrite(PIN_OUT_VENT_ONOFF, OFF);
  digitalWrite(PIN_OUT_LIGHT, OFF);

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  ArduinoOTA.setHostname("ESP32Hob2Hood");

  ArduinoOTA.onStart([]() {
    disableWDT();  // ⬅️ Disable WDT during OTA
  });

  ArduinoOTA.onEnd([]() {
    enableWDT();   // ⬅️ Re-enable after OTA
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    esp_task_wdt_reset();  // ⬅️ Feed WDT during OTA
  });

  ArduinoOTA.begin();

  SerialHTML.begin(&server);
  SerialHTML.onMessage(receiveMessage);
  SerialHTML.onConnect(connectRequest);
  SerialHTML.onDisconnect(disconnectRequest);
  server.begin();

  vTaskDelay(1000 / portTICK_PERIOD_MS);
  SAFE_PRINTLN("Starting Hob2Hood IR Receiver");
  SAFE_PRINTLN("WiFi connected");
  SAFE_PRINTF("IP address: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("ESP32Hob2Hood")) {
    SAFE_PRINTLN("mDNS responder started at ESP32Hob2Hood");
  }
  SAFE_PRINTF("Ready to receive IR at pin %d\n", IR_RECEIVE_PIN);
  SAFE_PRINTLN("OTA Ready");

  commandQueue = xQueueCreate(10, sizeof(String));
  xTaskCreate(IRTask, "IRTask", 4096, NULL, 1, NULL);

  enableWDT();  // ⬅️ Start WDT
}

void loop() {
  ArduinoOTA.handle();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }

  String queuedCmd;
  while (xQueueReceive(commandQueue, &queuedCmd, 0) == pdTRUE) {
  UBaseType_t qLen = uxQueueMessagesWaiting(commandQueue);
  if (qLen > 5) {
    SAFE_PRINTF("Warning: Command queue is filling (%d items)\n", qLen);
  }
  handleCommand(queuedCmd);
}

  esp_task_wdt_reset();  // ⬅️ Feed the watchdog
}