#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

#define DHT_PIN 5
#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);
Servo servo;
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long buzzerOffTime = 0;
bool buzzerActive = false;

void readDHT(){
  static unsigned long lastRead = 0;
  if (millis() - lastRead < 2000) return;
  lastRead = millis();
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  
  if(isnan(humidity) || isnan(temperature)){
    Serial.println("Failed to read from the dht sensor");
  }
  
  // Serial.print("Humidity: ");
  // Serial.println(humidity);
  // Serial.print("Temperature: ");
  // Serial.println(temperature);
  char temp[100];
  char humid[100];
  
  snprintf(temp, 100, "{\"value\": %.2f, \"unit\": \"C\"}", temperature);
  client.publish("home/sensors/temperature", temp);
  
  snprintf(humid, 100, "{\"value\": %.2f, \"unit\": \"%%\"}", humidity);
  client.publish("home/sensors/humidity", humid);
}

void readPIR(){
  pinMode(23, INPUT);
  bool isMotion = digitalRead(23);
  // Serial.print("Motion detection: ");
  // Serial.println(isMotion);
  char msg[100];
  snprintf(msg, 100, "{\"detected\": %d}", isMotion);
  client.publish("home/sensors/motion", msg);
}

void readLDR(){
  pinMode(34, INPUT);
  int lightIntensity = analogRead(34);
  // Serial.print("Light Intensity: ");
  // Serial.println(lightIntensity);
  char level[10];
  if (lightIntensity < 1000) {
      strcpy(level, "dark");
  } 
  else if (lightIntensity < 2500) {
      strcpy(level, "dim");
  } 
  else {
      strcpy(level, "bright");
  }
  char msg[100];
  snprintf(msg, 100, "{\"value\": %d, \"level\": \"%s\"}", lightIntensity, level);
  client.publish("home/sensors/light", msg);
}

void readHCSR04(){
  int trigPin = 19;
  int echoPin = 18;
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  long duration;
  float distanceCm;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH, 30000);
  if(duration == 0){
    Serial.println("No echo");
    return;
  }
  distanceCm = duration * 0.034 / 2;
  // Serial.print("Distance: ");
  // Serial.println(distanceCm);

  char msg[100];
  snprintf(msg, 100, "{\"value\": %.2f, \"unit\": \"cm\"}", distanceCm);
  client.publish("home/sensors/distance", msg);
}

void heartbeat() {
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {  
    lastHeartbeat = millis();
    unsigned long uptime = millis() / 1000;  
    int rssi = WiFi.RSSI();                 
    char msg[100];
    snprintf(msg, 100, "{\"uptime\": %lu, \"rssi\": %d}", uptime, rssi);
    client.publish("home/system/status", msg);
  }
}

void setup_wifi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32SHAHD")) {
      Serial.println("connected!");
      client.subscribe("actuators/led");
      client.subscribe("actuators/buzzer");
      client.subscribe("actuators/servo");
      client.subscribe("actuators/relay");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    Serial.print("Raw payload: ");
    for(int i = 0; i < length; i++) Serial.print((char)payload[i]);
    Serial.println();
    return;
  }

  if (strcmp(topic, "actuators/led") == 0) {
    String state = doc["state"].as<String>(); 
    String color = doc["color"].as<String>(); 
    Serial.print("LED command received: ");
    Serial.println(state);
    if (state == "on" && (color =="red")) digitalWrite(4, HIGH);
    else if(state == "on" && color == "yellow") digitalWrite(22, HIGH);
    else if (color =="red") digitalWrite(4, LOW);
    else if(color == "yellow") digitalWrite(22, LOW);
  }

  else if (strcmp(topic, "actuators/buzzer") == 0) {
    String state = doc["state"].as<String>();
    int duration = doc["duration"].as<int>();
    if (state == "on") {
      digitalWrite(16, HIGH);
      buzzerActive = true;
      buzzerOffTime = millis() + duration;
    } else {
      digitalWrite(16, LOW);
      buzzerActive = false;
    }
  }

  else if (strcmp(topic, "actuators/servo") == 0) {
    int angle = doc["angle"].as<int>();
    servo.write(angle);
    Serial.print("Servo command received: ");
    Serial.println(angle);
  }

  else if (strcmp(topic, "actuators/relay") == 0) {
    String state = doc["state"].as<String>();
    if (state == "on") digitalWrite(17, HIGH);
    else digitalWrite(17, LOW);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  pinMode(4, OUTPUT);   // red led
  pinMode(22, OUTPUT); // yellow led
  pinMode(16, OUTPUT);  // buzzer
  pinMode(17, OUTPUT);  // relay
  servo.attach(21);
  }

void loop() {
  if (buzzerActive && millis() >= buzzerOffTime) {
      digitalWrite(16, LOW);
      buzzerActive = false;
  }

  if (!client.connected()) reconnect();
  client.loop();

  readPIR();
  readLDR();
  readHCSR04();
  readDHT();
  heartbeat();
  delay(10); // this speeds up the simulation
}
