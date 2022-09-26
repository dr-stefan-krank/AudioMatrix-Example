#include <Arduino.h>
#include <ArduinoOTA.h>
#include <AudioMatrix.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <TCA6416A.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Wire.h>
#include "credentials.h"

#define INTERNAL_LED 2

char mqttId[18] = "";

uint8_t input;
uint8_t output;
bool connect;

TCA6416A expander;
AudioMatrix matrix;
WiFiClient espClient;
PubSubClient client(espClient);
long lastReconnectAttempt = 0;

// TFT Stuff
TFT_eSPI tft = TFT_eSPI();
#define TFT_BG 0x0000
#define TFT_FG 0xE77D
#define TFT_CONNECTION_LINE 0x34BA
#define TFT_OUTPUT_ON 0x2E4D
#define TFT_OUTPUT_OFF 0x9514
#define TFT_GRID 0x7C51
#define TFT_TITLE 0xE247

#define TFT_OUTPUT_LINE_HEIGHT 19
#define TFT_OUTPUT_LINE_START 20
#define TFT_INPUT_LINE_HEIGHT 40
#define TFT_INPUT_LINE_START 20

// Forward Declarations
boolean reconnect();
void errLeds(int dly = 100, int count = 1);
void sendMQTT(const char topic[], const char value[]);
void sendIP();
void callback(char *topic, byte *payload, unsigned int length);
void subscribe(const char topic[]);
void setVolume(const int channel, const int value);
void tft_main();

void setup() {
    delay(500);
    Serial.begin(115200);
    Serial.println("BLARG Matrix starting…");
    matrix.begin();
    Wire.begin(1);
    // expander.begin(1);

    // Setup Wifi
    WiFi.mode(WIFI_OFF);
    WiFi.setSleep(false);
    delay(1);
    WiFi.mode(WIFI_STA);
    delay(1);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    // Get chip id
    sprintf(mqttId, "esp/%llx",
            ESP.getEfuseMac());  // Get Chip ID and set it as MQTT Client ID.
    Serial.print("MAC:");
    Serial.println(mqttId);

    // Setup OTA
    ArduinoOTA.setHostname(mqttId);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();

    // Setup MQTT
    client.setServer(mqttServer, mqttPort);
    client.setCallback(callback);

    reconnect();

    sendMQTT("Matrix/Input", "0");
    sendMQTT("Matrix/Output", "0");
    sendMQTT("Matrix/Connect", "0");

    //tft.init();
    //tft.setRotation(
    //    1);  // Important: TFT_Drivers/ST7789_Rotation.h:  "writedata(TFT_MAD_MV
             // | TFT_MAD_COLOR_ORDER);" in case 1
    //tft_main();

    //    for (int output = 1; output <= 16; output++) {
    //        setVolume(output, 100);
    //    }

    
}

void loop() {
    ArduinoOTA.handle();

    if (!client.connected()) {  // Check if PubSubClient is still connected
        reconnect();
    } else {
        client.loop();
    }

    delay(200);
}

boolean reconnect() {
    if (!WiFi.isConnected()) {  // Check if Wifi is still connected
        Serial.println("WiFi reconnect.");
        WiFi.disconnect();
        delay(50);
        WiFi.begin(ssid, password);  // Connect to the network
        delay(2000);                 // Initial delay
        int i = 0;
        while (WiFi.status() != WL_CONNECTED)  // Wait for the Wi-Fi to connect
        {                                      // Wait for the Wi-Fi to connect
            delay(1000);
            i++;
            if (i > 10) {  // reset the ESP after 10 tries
                ESP.restart();
            }
            Serial.println("WiFi connected");
        }
    }

    Serial.println("MQTT reconnect.");
    if (client.connect(mqttId, mqttUser, mqttPassword)) {
        Serial.println("MQTT connected");
        delay(200);
        // client.setKeepAlive(true);
        sendIP();
        subscribe("Output/Volume/1");
    } else {
        Serial.print("failed with state ");
        Serial.print(client.state());
        delay(2000);
    }

    return client.connected();
}

void errLeds(int dly, int count) {
    for (int i = 0; i < count; i++) {
        digitalWrite(INTERNAL_LED, HIGH);
        delay(dly);
        digitalWrite(INTERNAL_LED, LOW);
        delay(dly);
    }
}

void sendMQTT(const char topic[], const char value[]) {
    char mqtt_topic[strlen(mqttId) + 2 + strlen(topic)];
    sprintf(mqtt_topic, "%s/%s", mqttId, topic);
    client.publish(mqtt_topic, value);
    Serial.printf("MQTT Send: %s : %s\n", mqtt_topic, value);
}

void sendIP() {
    // Send current IP to MQTT
    String ipaddress = WiFi.localIP().toString();
    char ipchar[ipaddress.length() + 1];
    ipaddress.toCharArray(ipchar, ipaddress.length() + 1);

    sendMQTT("IP", ipchar);
}

void subscribe(const char topic[]) {
    char mqtt_topic[strlen(mqttId) + strlen(topic) + 2];
    sprintf(mqtt_topic, "%s/%s", mqttId, topic);
    Serial.printf("MQTT subscribe to: %s\n", mqtt_topic);
    client.subscribe(mqtt_topic);
}

void setVolume(const int channel, const int value) {
    int db = map(value, 0, 99, 79, 0);
    Serial.printf("Set Volume to %i (-%idB) on channel %i\n", value, db,
                  channel);
    matrix.setVolume(channel, db);

    int line = TFT_OUTPUT_LINE_HEIGHT * channel;
    tft.setTextSize(0);
    tft.setCursor(450, line);
    tft.printf("%i%%", value);
}

void callback(char *topic, byte *payload, unsigned int length) {
    char p[length];
    strncpy(p, (char *)payload,
            length);   // convert the byte payload to a readable string.
    p[length] = '\0';  // Null terminate it at the end.
    std::string t(topic);
    String value = p;

    Serial.print("Received Topic: ");
    Serial.println(topic);
    Serial.print("Received Payload: ");
    Serial.println(p);

    if (t.find("Matrix/Input") != std::string::npos) {
        input = value.toInt();
    }
    if (t.find("Matrix/Output") != std::string::npos) {
        output = value.toInt();
    }
    if (t.find("Matrix/Connect") != std::string::npos) {
        if (p[0] == 0) {
            matrix.disconnect(input, output);
        } else {
            matrix.connect(input, output);
        }
    }

    if (t.find("Output/Volume/1\0") != std::string::npos) {
        setVolume(1, value.toInt());
    }
    if (t.find("Output/Volume/2\0") != std::string::npos) {
        setVolume(2, value.toInt());
    }
    if (t.find("Output/Volume/3\0") != std::string::npos) {
        setVolume(3, value.toInt());
    }
    if (t.find("Output/Volume/4\0") != std::string::npos) {
        setVolume(4, value.toInt());
    }
    if (t.find("Output/Volume/5\0") != std::string::npos) {
        setVolume(5, value.toInt());
    }
    if (t.find("Output/Volume/6\0") != std::string::npos) {
        setVolume(6, value.toInt());
    }
    if (t.find("Output/Volume/7\0") != std::string::npos) {
        setVolume(7, value.toInt());
    }
    if (t.find("Output/Volume/8\0") != std::string::npos) {
        setVolume(8, value.toInt());
    }
    if (t.find("Output/Volume/9\0") != std::string::npos) {
        setVolume(9, value.toInt());
    }
    if (t.find("Output/Volume/10\0") != std::string::npos) {
        setVolume(10, value.toInt());
    }
    if (t.find("Output/Volume/11\0") != std::string::npos) {
        setVolume(11, value.toInt());
    }
    if (t.find("Output/Volume/12\0") != std::string::npos) {
        setVolume(12, value.toInt());
    }
    if (t.find("Output/Volume/13\0") != std::string::npos) {
        setVolume(13, value.toInt());
    }
    if (t.find("Output/Volume/14") != std::string::npos) {
        setVolume(14, value.toInt());
    }
    if (t.find("Output/Volume/15\0") != std::string::npos) {
        setVolume(15, value.toInt());
    }
    if (t.find("Output/Volume/16\0") != std::string::npos) {
        setVolume(16, value.toInt());
    }
}

void tft_main() {
    int line = 0;
    tft.fillScreen(TFT_BG);
    tft.setTextColor(TFT_TITLE);
    tft.drawString("-- BLARG MATRIX --", 150, line);
    tft.setTextSize(1);
    tft.setTextColor(TFT_FG);
    tft.drawString("Input", 5, line);
    tft.drawString("Output", 402, line);
    tft.drawString("Volume", 442, line);
    tft.drawLine(0, line + 10, 480, line + 10, TFT_GRID);
    line = TFT_INPUT_LINE_START;

    // Draw Inputs
    tft.setTextSize(2);
    for (int input = 1; input <= 8; input++) {
        tft.setCursor(12, line);
        tft.printf("%i", input);
        tft.drawLine(0, line + 25, 40, line + 25, TFT_GRID);
        line = line + TFT_INPUT_LINE_HEIGHT;
    }

    tft.drawLine(0, 0, 0, 320, TFT_GRID);
    tft.drawLine(40, 0, 40, 320, TFT_GRID);

    tft.drawLine(400, 0, 400, 320, TFT_GRID);
    tft.drawLine(440, 0, 440, 320, TFT_GRID);

    // Draw Outputs
    line = TFT_OUTPUT_LINE_START;
    tft.setTextSize(0);
    for (int output = 1; output <= 16; output++) {
        tft.setCursor(420, line);
        tft.printf("%i", output);
        tft.drawLine(400, line + 10, 480, line + 10, TFT_GRID);
        line = line + TFT_OUTPUT_LINE_HEIGHT;
    }

    // tft.setTextSize(0);
}

