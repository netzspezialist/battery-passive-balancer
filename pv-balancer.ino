#include <NTPClient.h>
#include "PCF8575.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

#define SDA 2
#define SCL 0

#define DEBUG

const char *ssid = "netzspezialist2";
const char *password = "87702898450441182013";

// MQTT Broker settings
const char *mqtt_broker = "192.168.88.3";
const char *mqtt_topic_bms_cellVoltageRange = "bms/cellVoltageRange";
const char *mqtt_topic_ballancer = "ballancer/activation";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqtt_client(espClient);
PCF8575 pcf(0x20);

int pins[] = { 7, 8, 6, 9, 5, 10, 4, 11, 3, 12, 2, 13, 1, 14, 0, 15 };
long cellBalanceStartTime[] {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int lastHighestCell = -1;

void logMsg(String msg, bool newLine = true);

void logMsg(String msg, bool newLine) {
#ifdef DEBUG
  if (newLine == true) {
    Serial.println(msg);
  } else {
    Serial.print(msg);
  }
#endif
}


void connectToWiFi() {
  WiFi.begin(ssid, password);

  logMsg("Connecting WiFi to " + String(ssid));

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logMsg(".", false);
  }

  logMsg("Connected to the WiFi network", true);
}

void connectToMQTTBroker() {
  while (!mqtt_client.connected()) {
    String client_id = "battery-balancer-" + String(WiFi.macAddress());
    logMsg("Connecting to MQTT Broker as ", false);
    logMsg(client_id.c_str(), false);
    logMsg("...");
    if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      logMsg("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_topic_bms_cellVoltageRange);
      // Publish message upon successful connection
      //mqtt_client.publish(mqtt_topic, "Hi EMQX I'm ESP8266 ^^");
    } else {
      logMsg("Failed to connect to MQTT broker, rc=", false);
      logMsg(String(mqtt_client.state()), false);
      logMsg("try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  logMsg(" - Message received on topic: ", false);
  logMsg(topic);

  String mqttMsg = "";

  for (unsigned int i = 0; i < length; i++) {
    mqttMsg += (char)payload[i];
  }

  logMsg(mqttMsg);

  JsonDocument jsonLastMessage;
  deserializeJson(jsonLastMessage, mqttMsg);

  double highestVoltage = jsonLastMessage["highestVoltage"];
  double lowestVoltage = jsonLastMessage["lowestVoltage"];
  double cellDiff = highestVoltage - lowestVoltage;
  bool balanceRequired = highestVoltage > 3.430;
  bool cellDiffBalanceRequired = cellDiff >= 0.05;

  int highestCell = jsonLastMessage["highestCell"];
  highestCell--;

  if ( balanceRequired && cellDiffBalanceRequired )  {
      if (lastHighestCell != highestCell) {
        logMsg("Activate passive ballancing at cell " + String(highestCell) + " at " + String(highestVoltage) + "V");

        String mqttMessage = "{\"cell\":" + String(highestCell) + ",\"volltage\":" + String(highestVoltage) + ",\"timestamp\":" + millis()  + "}";
        mqtt_client.publish(mqtt_topic_ballancer, mqttMessage.c_str());
        lastHighestCell = highestCell;
      }
      #ifndef DEBUG        
        pcf.write(pins[highestCell], LOW);
      #endif
      cellBalanceStartTime[highestCell] = millis();
  }  
}


void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(1000);
#else
  Wire.pins(SDA, SCL);
  Wire.begin();
  pcf.begin();
  delay(1000);
  for (unsigned int i = 0; i <= 15; i++) {
    pcf.write(pins[i], LOW);
    delay(100);
      pcf.write16(0xFFFF);
    delay(100);
  }
#endif

  connectToWiFi();

  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTTBroker();
}


void loop() {
  if (!mqtt_client.connected()) {
    connectToMQTTBroker();
  }
  mqtt_client.loop();

  long currentMillis = millis();

  for (unsigned int i = 0; i <= 15; i++) {
    if ( (cellBalanceStartTime[i] > 0) && ((cellBalanceStartTime[i] + 180000) < currentMillis) ) {
      logMsg("Deactivate passive ballancing at cell " + String(i));
      pcf.write(pins[i], HIGH);
      cellBalanceStartTime[i] = 0;      
    }
  }
}
