
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char *ssid = "KuMo.";
const char *password = "578923tdf";

WiFiClient wifiClient;
// MQTT config
const char *mqttServer = "mqtt.netpie.io";
const int mqttPort = 1883;
const char *mqttClientId = "587ab64a-bfa6-4c2e-8341-f9be792a92df";
const char *mqttUser = "sy1To3GuqL9gXqFkwh6bYxcHNDL3kWcQ";
const char *mqttPassword = "gZfUy4ZGLpPznSQqPhejMpn3JSJWsDWV";
const char *topic_pub = "@msg/lab_ict/speed_data";
const char *topic_sub = "@msg/lab_ict/speed_control";
const char *data_pub = "@shadow/data/update";
PubSubClient mqttClient(wifiClient);
String publishMessage;

#define TFT_CS 33
#define TFT_RST 32
#define TFT_DC 25

#define TRIG1 16
#define ECHO1 17

#define TRIG2 5
#define ECHO2 36

#define RED_LED 26
#define GREEN_LED 4
#define BUZZER 27

#define SENSOR_DISTANCE_CM 35.5
#define TRIGGER_DISTANCE 20.0

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

#define BUZZER_CHANNEL 0
#define BUZZER_FREQ 1000
#define BUZZER_RESOLUTION 8
int speedCounter = 1;
unsigned long t_start = 0;
unsigned long t_end = 0;
unsigned long t_buzzer = 0;
unsigned long lastDetectTime = 0;
const int detectCooldown = 50;

bool sensor_triggered = false;

bool reverse_detected = false;
unsigned long reverse_time = 0;

float speed_kmh = 0;
float speed_limit = 4.0;
int buzzerState = 0;

String status = "";

void setup_wifi()
{
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void sendToFirebase(float speed_kmh, String status)
{
  HTTPClient http;

  String url = "https://iotspeed-73afe-default-rtdb.asia-southeast1.firebasedatabase.app/sensorData.json";

  String speedID = "speed" + String(speedCounter++);

  String payload = "{ \"" + speedID + "\": {";
  payload += "\"speed\": " + String(speed_kmh, 2) + ",";
  payload += "\"status\": \"" + status + "\"";
  payload += "} }";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PATCH(payload);

  if (httpResponseCode > 0)
  {
    Serial.println("Firebase response: " + http.getString());
  }
  else
  {
    Serial.println("Error sending to Firebase");
  }

  http.end();
}
void reconnectMQTT()
{
  while (!mqttClient.connected())
  {
    Serial.println("Attempting MQTT connection...");

    if (mqttClient.connect(mqttClientId, mqttUser, mqttPassword))
    {
      Serial.println("MQTT connected");
      mqttClient.subscribe(topic_sub);
      Serial.println("Subscribed to topic: " + String(topic_sub));
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message;

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.println(message);

  // Buzzer control
 if (message.indexOf("\"buzzer\":1") >= 0)
{
  buzzerState = 1;
  Serial.println("Buzzer Enabled");
}

if (message.indexOf("\"buzzer\":0") >= 0)
{
  buzzerState = 0;
  Serial.println("Buzzer Disabled");
}

  
  // if (message.indexOf("\"speedLimit\":") != -1)
  // {
  //   int index = message.indexOf("\"speedLimit\":");
  //   String value = message.substring(index + 13);
  //   value.replace("}", "");

  //   speed_limit = value.toFloat();

  //   Serial.print("New Speed Limit = ");
  //   Serial.println(speed_limit);
  // }
}

float readDistance(int trigPin, int echoPin)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  float distance = (duration * 0.0343) / 2;
  delay(5);
  return distance;
}

void setup()
{
  Serial.begin(115200);

  setup_wifi();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

  tft.begin();
  tft.setRotation(1);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RESOLUTION);
ledcAttachPin(BUZZER, BUZZER_CHANNEL);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    setup_wifi();
  }
  if (!mqttClient.connected())
  {
    reconnectMQTT();
  }

  mqttClient.loop();

  float d1 = readDistance(TRIG1, ECHO1);
  float d2 = readDistance(TRIG2, ECHO2);

  // ตรวจจับย้อนศร (Sensor2 ก่อน)
  if (!sensor_triggered && d2 < TRIGGER_DISTANCE && millis() - lastDetectTime > detectCooldown)
  {
    reverse_detected = true;
    reverse_time = millis();
  }

  // ถ้า Sensor2 มาก่อน แล้ว Sensor1 ย้อนศร
  if (reverse_detected && d1 < TRIGGER_DISTANCE)
  {
    Serial.println("WRONG WAY DETECTED");
    status = "WRONG_WAY";
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextSize(3);
    tft.setCursor(40, 100);
    tft.setTextColor(ILI9341_RED);
    tft.println("WRONG WAY!");

    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);

    if (buzzerState == 1)
    {
    ledcWrite(BUZZER_CHANNEL, 150);
    delay(800);
    ledcWrite(BUZZER_CHANNEL, 0);
    }
    reverse_detected = false;
    lastDetectTime = millis();
    return;
  }

  if (!sensor_triggered && d1 < TRIGGER_DISTANCE && millis() - lastDetectTime > detectCooldown)
  {
    t_start = millis();
    sensor_triggered = true;

  }

  if (sensor_triggered && d2 < TRIGGER_DISTANCE)
  {
    t_end = millis();
    t_buzzer = t_end + 1000;
    float deltaT = (float)(t_end - t_start) / 1000.0;  


    if (deltaT > 0.05 && deltaT < 3) // เพื่อกรองค่าผิดพลาดที่อาจเกิดจากการตรวจจับที่ไม่สมบูรณ์
    {
      float speed_cm_s = SENSOR_DISTANCE_CM / deltaT;
      speed_kmh = speed_cm_s * 0.036;

      Serial.print("Speed = ");
      Serial.print(speed_kmh);
      Serial.println(" km/h");

      tft.fillScreen(ILI9341_BLACK);

      tft.setTextSize(3);

      tft.setCursor(20, 30);
      tft.setTextColor(ILI9341_GREEN);
      tft.print("Speed:");

      tft.setCursor(20, 80);
      tft.print(speed_kmh, 2);
      tft.print(" km/h");

      tft.setCursor(20, 140);

      if (speed_kmh > speed_limit)
      {
        status = "OVERSPEED";

        tft.setTextColor(ILI9341_RED);
        tft.println("OVERSPEED!");

        digitalWrite(RED_LED, HIGH);
        digitalWrite(GREEN_LED, LOW);

        if (buzzerState == 1)
        {
        ledcWrite(BUZZER_CHANNEL, 10);
        delay(800);
        ledcWrite(BUZZER_CHANNEL, 0);
        }

      }
      else
      {
        status = "NORMAL";

        tft.setTextColor(ILI9341_GREEN);
        tft.println("NORMAL");

        digitalWrite(RED_LED, LOW);
        digitalWrite(GREEN_LED, HIGH);

        ledcWrite(BUZZER_CHANNEL, 0);
      }
      publishMessage = "{\"data\":{\"speed\":" + String(speed_kmh, 2) + ",\"status\":\"" + status + "\"}}";

      mqttClient.publish(topic_pub, publishMessage.c_str());
      mqttClient.publish(data_pub, publishMessage.c_str());
      sendToFirebase(speed_kmh, status);

      Serial.print("Speed Limit = ");
      Serial.println(speed_limit);

    }

    sensor_triggered = false;
    reverse_detected = false;
    lastDetectTime = millis();
  }
}