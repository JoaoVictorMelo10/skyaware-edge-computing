// ============================================================
//  DarkSky Edge — Versão FIWARE (WiFi + MQTT)
//  Broker MQTT: IoT Agent FIWARE na VM Azure
//  Orion Context Broker: porta 1026
//  IoT Agent MQTT: porta 1883
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>
#include <ArduinoJson.h>

// ============================================================
// CONFIGURAÇÕES — edite aqui antes de rodar
// ============================================================
const char* WIFI_SSID     = "SEU_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA";

// IP da VM Azure com o FIWARE
const char* MQTT_BROKER   = "SEU_IP_DA_VM";
const int   MQTT_PORT     = 1883;

// Identificação do dispositivo no FIWARE
const char* DEVICE_ID     = "darksky-esp32-001";
const char* ENTITY_TYPE   = "DarkSkyStation";
const char* API_KEY       = "darksky2026";

// Tópicos MQTT
const char* TOPIC_SUB     = "/darksky2026/darksky-esp32-001/cmd";
const char* TOPIC_PUB     = "/darksky2026/darksky-esp32-001/attrs";

// ============================================================
// HARDWARE
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define DHTPIN   15
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

#define LDR_PIN    34
#define GREEN_LED  18
#define RED_LED    19
#define BUZZER_PIN 27

Adafruit_BMP085 bmp;

WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ============================================================
// ESTADO GLOBAL
// ============================================================
float orbitalQuality  = 5.0;   // valor padrão; atualizado via MQTT
unsigned long lastSensorRead   = 0;
unsigned long lastMqttMsg      = 0;
unsigned long lastBuzzerToggle = 0;
bool  buzzerState   = false;
bool  waitingData   = true;

const unsigned long SENSOR_INTERVAL = 5000;
const unsigned long BUZZER_INTERVAL = 300;
const unsigned long MQTT_TIMEOUT    = 60000;

// ============================================================
// CALLBACK MQTT — recebe orbitalQuality do FIWARE
// Formato do IoT Agent: "darksky-esp32-001@orbitalQuality|7.5"
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.print(F("[MQTT RX] "));
  Serial.println(msg);

  lastMqttMsg = millis();
  waitingData = false;

  // Extrai o valor após o pipe: "device@orbitalQuality|7.5"
  int pipeIndex = msg.indexOf('|');
  if (pipeIndex != -1) {
    String valueStr = msg.substring(pipeIndex + 1);
    float val = valueStr.toFloat();
    if (val > 0) {
      orbitalQuality = constrain(val, 0.0F, 10.0F);
      Serial.print(F("orbitalQuality atualizado: "));
      Serial.println(orbitalQuality);
    }
  }
}

// ============================================================
// WiFi
// ============================================================
void connectWiFi() {
  Serial.print(F("Conectando WiFi: "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(F("Conectando WiFi..."));
  display.display();

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi OK"));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\nWiFi FALHOU — operando offline"));
  }
}

// ============================================================
// MQTT
// ============================================================
void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  Serial.print(F("Conectando MQTT..."));
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    String clientId = String(DEVICE_ID);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(F(" OK"));
      mqtt.subscribe(TOPIC_SUB);
      Serial.print(F("Subscribe em: "));
      Serial.println(TOPIC_SUB);
    } else {
      Serial.print(F(" falhou, rc="));
      Serial.print(mqtt.state());
      delay(2000);
      attempts++;
    }
  }
}

// ============================================================
// Publicar leituras no FIWARE via MQTT (Ultralight 2.0)
// Ex: t|22.5|h|65.0|p|1013.2|l|2048|s|7.3
// ============================================================
void publishSensorData(float temp, float hum, float pressure, int ldrRaw, float skyScore) {
  if (!mqtt.connected()) return;

  char payload[120];
  snprintf(payload, sizeof(payload),
           "t|%.1f|h|%.1f|p|%.1f|l|%d|s|%.2f",
           temp, hum, pressure, ldrRaw, skyScore);

  bool ok = mqtt.publish(TOPIC_PUB, payload);
  Serial.print(F("[MQTT TX] -> "));
  Serial.println(payload);
  if (!ok) Serial.println(F("[MQTT TX] FALHOU"));
}

// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED,  OUTPUT);
  pinMode(RED_LED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(GREEN_LED,  LOW);
  digitalWrite(RED_LED,    LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED: falha"));
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  if (!bmp.begin()) {
    Serial.println(F("BMP180: nao encontrado"));
  }

  connectWiFi();
  connectMQTT();

  lastMqttMsg = millis();
}

// ============================================================
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();

  if (now - lastMqttMsg > MQTT_TIMEOUT) {
    waitingData = true;
  }

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    float temp     = dht.readTemperature();
    float hum      = dht.readHumidity();
    float pressure = bmp.readPressure() / 100.0F; // hPa
    int   ldrRaw   = analogRead(LDR_PIN);

    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("DHT22: falha na leitura"));
      return;
    }

    // ---- Fatores locais ----
    float darkness  = (float)(4095 - ldrRaw) / 4095.0F * 10.0F;
    float humFactor = constrain(10.0F - (hum / 10.0F), 0.0F, 10.0F);
    float pressureFactor = constrain((pressure - 980.0F) / 5.0F, 0.0F, 10.0F);

    // ---- Sky Score ----
    // Pesos: orbital 40% | escuridão 30% | umidade 20% | pressão 10%
    float skyScore = (orbitalQuality * 0.4F)
                   + (darkness       * 0.3F)
                   + (humFactor      * 0.2F)
                   + (pressureFactor * 0.1F);
    skyScore = constrain(skyScore, 0.0F, 10.0F);

    // ---- Publicar no FIWARE ----
    publishSensorData(temp, hum, pressure, ldrRaw, skyScore);

    // ---- Atuadores ----
    if (waitingData) {
      static bool ledBlink = false;
      ledBlink = !ledBlink;
      digitalWrite(RED_LED,   ledBlink);
      digitalWrite(GREEN_LED, LOW);
      noTone(BUZZER_PIN);
    } else if (skyScore >= 7.0F) {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED,   LOW);
      tone(BUZZER_PIN, 1000, 200);
    } else if (skyScore >= 4.0F) {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED,   LOW);
      noTone(BUZZER_PIN);
    } else {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED,   HIGH);
      if (now - lastBuzzerToggle >= BUZZER_INTERVAL) {
        lastBuzzerToggle = now;
        buzzerState = !buzzerState;
        if (buzzerState) tone(BUZZER_PIN, 500);
        else             noTone(BUZZER_PIN);
      }
    }

    // ---- OLED ----
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (waitingData) {
      display.println(F("Aguardando FIWARE..."));
    } else {
      display.println(F("=== DarkSky Edge ==="));
    }

    display.setCursor(0, 12);
    display.print(F("Temp: ")); display.print(temp, 1); display.println(F("C"));
    display.setCursor(0, 22);
    display.print(F("Umid: ")); display.print(hum, 1); display.println(F("%"));
    display.setCursor(0, 32);
    display.print(F("Press:")); display.print(pressure, 1); display.println(F("hPa"));
    display.setCursor(0, 42);
    display.print(F("OrbQ: ")); display.println(orbitalQuality, 1);
    display.setCursor(0, 54);
    display.print(F("Score:")); display.print(skyScore, 1);
    if (!waitingData) {
      if      (skyScore >= 7.0F) display.println(F(" IDEAL"));
      else if (skyScore >= 4.0F) display.println(F(" MOD"));
      else                       display.println(F(" RUIM"));
    }
    display.display();

    // ---- Serial ----
    Serial.println(F("===== DARKSKY FIWARE ====="));
    Serial.print(F("Temp:      ")); Serial.println(temp);
    Serial.print(F("Umidade:   ")); Serial.println(hum);
    Serial.print(F("Pressao:   ")); Serial.println(pressure);
    Serial.print(F("LDR:       ")); Serial.println(ldrRaw);
    Serial.print(F("Orbital Q: ")); Serial.println(orbitalQuality);
    Serial.print(F("Sky Score: ")); Serial.println(skyScore);
    Serial.println();
  }
}
