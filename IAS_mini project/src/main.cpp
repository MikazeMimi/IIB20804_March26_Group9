#define BLYNK_TEMPLATE_ID "TMPL6kw7BXOoo"
#define BLYNK_TEMPLATE_NAME "IAS"
#define BLYNK_AUTH_TOKEN "yYgutPfHDbKJlBnlhHZxG04lMpj2CDVJ"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "DHT.h"
#include <time.h>
#include <BlynkSimpleEsp32.h>

// WiFi credentials
const char* ssid = "Syahmi";
const char* password = "mikaze24";

// Mosquitto broker
const char* mqtt_server = "10.235.94.19"; 
const int mqtt_port = 8883;

WiFiClientSecure espClient;
PubSubClient client(espClient);

// PIR pins
#define PIR_PIN     13
#define RED_LED     18
#define BLUE_LED    19
#define RECORD_LED  5
#define BUZZER_PIN  22

// DHT pins
#define DHTPIN 25
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

volatile bool motionDetected = false;
bool sirenActive = false;

// PIR interrupt
void IRAM_ATTR pirISR() {
  if (digitalRead(PIR_PIN) == HIGH) motionDetected = true;
}

// MQTT callback
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("MQTT [%s]: %s\n", topic, msg.c_str());

  if (msg == "siren=ON") sirenActive = true;
  if (msg == "siren=OFF") sirenActive = false;
}

// MQTT reconnect
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to Mosquitto...");
    if (client.connect("ESP32sensor")) {   // MQTT client ID
      Serial.println("connected");
      client.subscribe("/oneM2M/resp/Mobius2/ESP32sensor/json");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// Blynk button handler (V1 = Button widget)
BLYNK_WRITE(V1) {
  int value = param.asInt();
  if (value == 1) {
    sirenActive = true;
  } else {
    sirenActive = false;
    digitalWrite(RED_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    noTone(BUZZER_PIN);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RECORD_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("WiFi connected!");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Wait until time is set
  Serial.print("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {   // arbitrary check: time > Jan 1, 1970
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("Time synced!");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
    return;
  }

  File ca = SPIFFS.open("/ca-crt.pem", "r");
  if (!ca) {
    Serial.println("Failed to open CA file");
    return;
  }
  String ca_str = ca.readString();
  espClient.setCACert(ca_str.c_str());
  ca.close();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  dht.begin();
}

void loop() {
  Blynk.run();
  if (!client.connected()) reconnect();
  client.loop();

  // PIR motion publish
  if (motionDetected) {
    digitalWrite(RECORD_LED, HIGH);
    String from_id = "ESP32sensor"; 
    String topic = "/oneM2M/req/" + from_id + "/Mobius2/json";

    String payload = "{\"m2m:rqp\":{\"op\":1,"
                    "\"to\":\"/Mobius/Surveillance/MotionCNT\","
                    "\"fr\":\"" + from_id + "\","
                    "\"rqi\":\"12345\","
                    "\"ty\":4,"
                    "\"pc\":{\"m2m:cin\":{\"con\":\"Motion detected!\"}}}}";

    client.publish(topic.c_str(), payload.c_str());
    Serial.println("Published motion event");
    digitalWrite(RECORD_LED, LOW);

    // Blynk updates
    Blynk.virtualWrite(V4, "Motion detected!");
    Blynk.virtualWrite(V0, 1); // LED widget ON

    motionDetected = false;
  }

  // DHT publish
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  if (!isnan(temperature) && !isnan(humidity)) {
    String from_id = "ESP32sensor"; 
    String topic   = "/oneM2M/req/" + from_id + "/Mobius2/json";

    String conData = "{\"temp\":" + String(temperature, 1) + 
                    ",\"hum\":" + String(humidity, 1) + "}";

    String payload = "{\"m2m:rqp\":{\"op\":1,"
                    "\"to\":\"/Mobius/Surveillance/DHTCNT\","
                    "\"fr\":\"" + from_id + "\","
                    "\"rqi\":\"67890\","
                    "\"ty\":4,"
                    "\"pc\":{\"m2m:cin\":{\"con\":" + conData + "}}}}";

    client.publish(topic.c_str(), payload.c_str());
    Serial.println("Published DHT event: " + conData);

    // Blynk updates
    Blynk.virtualWrite(V2, temperature);
    Blynk.virtualWrite(V3, humidity);
  }

  // Siren routine
  if (sirenActive) {
    digitalWrite(RED_LED, HIGH);
    tone(BUZZER_PIN, 1200);
    delay(300);
    digitalWrite(RED_LED, LOW);
    digitalWrite(BLUE_LED, HIGH);
    tone(BUZZER_PIN, 600);
    delay(300);
    noTone(BUZZER_PIN);
  }

  delay(2000);
}