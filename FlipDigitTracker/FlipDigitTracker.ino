/*
 * Realtime FlipDigit crypto tracker
 *
 * Realtime crypto tracker on mechanical FlipDigit display using an Arduino.
 *
 * Author: Owen McAteer
 * Created on: 16.11.2021
*/

#include <Arduino.h>
#include <math.h>

#include <SoftwareSerial.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsClient.h>
#include <Hash.h>
#include <ArduinoJson.h>

/**
 * Settings
 */
// Wifi & API
#define WIFI_USER "NETWORKNAME"
#define WIFI_PASS "NETWORKPASSWORD"
#define API_URL "ws-feed.exchange.coinbase.com"
// Trading data
String track1_name = "BTC-USD";
String track1_label = "BTC";
String track2_name = "ETH-USD";
String track2_label = "ETH";
// Display FPS
const int fps = 30;

// RS485 pins
#define RS485_RX      D2  // Serial Receive pin
#define RS485_TX      D3  // Serial Transmit pin
#define RS485_CONTROL D0 // Transmission set pin
SoftwareSerial RS485Serial (RS485_RX, RS485_TX);

// Json setup
StaticJsonDocument<64> filter;
DynamicJsonDocument doc(256);

// Wifi setup
ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

// ASCII 48-57
byte numbers[10] = {
  0x7E, // 0
  0x30, // 1
  0x6D, // 2
  0x79, // 3
  0x33, // 4
  0x5B, // 5
  0x5F, // 6
  0x70, // 7
  0x7F, // 8
  0x7B  // 9
};

// ASCII 64-90
byte alphabet[] = {
  0x77, // a
  0x1F, // b
  0x0D, // c
  0x3D, // d
  0x4F, // e
  0x47, // f
  0x5E, // g
  0x17, // h
  0x04, // i
  0x3C, // j
  0x57, // k (unusable)
  0x0E, // l
  0x54, // m (unusable)
  0x15, // n
  0x1D, // o
  0x67, // p
  0x73, // q
  0x05, // r
  0x5B, // s
  0x0F, // t
  0x1C, // u
  0x3A, // v (unusable)
  0x2A, // w (unusable)
  0x37, // x (unusable)
  0x3B, // y
  0x6D, // z
};

// Display buffer
char displayBuffer[28] = "";

// Timing
unsigned long previousMillis = 0;
const long interval = 1000 / fps;

// Prices
int track1_value;
int track2_value;


/**
 * Setup
 *
 * - Start RS485
 * - Connect to Wifi
 * - Connect to API WebSocket
 * - JSON filtering
 */
void setup() {
  Serial.begin(115200);

  // RS485 setup
  pinMode(RS485_RX, INPUT);
  pinMode(RS485_TX, OUTPUT);
  pinMode(RS485_CONTROL, OUTPUT);
  digitalWrite(RS485_CONTROL, HIGH);
  RS485Serial.begin(19200);

  // Set loading message
  setDisplayLine(1, "LOADING");
  castDisplay();

  // Connect to Wifi
  WiFiMulti.addAP(WIFI_USER, WIFI_PASS);
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(100);
  }
  Serial.println("[SETUP] CONNECTED TO WIFI");

  // Connect to API
  webSocket.beginSSL(API_URL, 443);
  webSocket.onEvent(webSocketEvent);

  // Setup Json filtering
  filter["product_id"] = true;
  filter["price"] = true;
}

/**
 * Loop tick
 */
void loop() {
  // Check WebSocket
  webSocket.loop();

  // Track time for FPS
  unsigned long currentMillis = millis();

  // Update display (FPS)
  if (currentMillis - previousMillis >= interval) {
    // Save the last time display updated
    previousMillis = currentMillis;

    // Format data
    if(track1_value) {
      setDisplayLine(1, track1_label);
      setDisplayLine(2, "  " + String(track1_value));
      setDisplayLine(3, track2_label);
      setDisplayLine(4, "   " + String(track2_value));
    }

    // Cast to displauy
    castDisplay();
  }
}


/**
 * WebSocket events
 */
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    // Disconnected
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected!");
      break;

    // Connected
    case WStype_CONNECTED:
      {
        Serial.println("[WS] Connected to API");
        // Heartbeat
        // webSocket.sendTXT("{\"type\": \"subscribe\",\"channels\": [{ \"name\": \"heartbeat\", \"product_ids\": [\"ETH-EUR\"] }]}");
        // Trackers
        webSocket.sendTXT("{\"type\": \"subscribe\",\"product_ids\": [\"" + track1_name + "\",\"" + track2_name + "\"],\"channels\": [\"ticker\"]}");
      }
      break;

    // Received data
    case WStype_TEXT:
      {
        // Decode JSON payload
        DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
        if (!error) {
          String product_id = doc["product_id"].as<String>();
          float price = doc["price"].as<float>();

          if (product_id.equalsIgnoreCase(track1_name)) {
            track1_value = round(price);
          }
          else if (product_id.equalsIgnoreCase(track2_name)) {
            track2_value = round(price);
          }
        }
        else {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
      }
      break;
  }
}


/**
 * Send buffer to FlipDigit display over RS485
 */
void castDisplay()
{
  RS485Serial.write(0x80); // Start command
  RS485Serial.write(0x83); // Auto refresh
  RS485Serial.write(0x01); // Panel ID

  // Digits
  for (int i = 0; i < 28; i++) {
    RS485Serial.write(mapTextToByte(displayBuffer[i]));
  }

  RS485Serial.write(0x8F); // Finish command
}


/**
 * Enter text onto display buffer.
 */
void setDisplayLine(int line, String text)
{
  int pos = (line - 1) * 7;
  int j = 0;
  for (int i = pos; i < pos + 7; i++) {
    displayBuffer[i] = text[j];
    j++;
  }
}


/**
 * Map char to ASCII for FlipDigit display.
 *
 * @see ASCII map above.
 */
byte mapTextToByte(char letter)
{
  int num = int(letter);

  if (num >= 48 && num <= 57) {
    // Number
    return numbers[num - 48];
  } else if (num >= 65 && num <= 90) {
    // Alphabet
    return alphabet[num - 65];
  } else if (num == 95) {
     // _
    return 0x08;
  } else if (num == 61) {
     // =
    return 0x09;
  } else if (num == 45) {
     // -
    return 0x01;
  } else {
    // Unknown. Default (blank/space)
    return 0x00; // (empty space) ASCII:20
  }
}
