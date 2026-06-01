// ============================================================
//  DarkSky Edge — ESP32 RAW (padrão professor Cabrini)
//  ESP32 só coleta dados e obedece comandos do Python
//  Python na VM faz o processamento e decide os LEDs
// ============================================================
// Nome e RM 
//------
// João Victor Melo Santos  566640 
// Murilo Jeronimo Ferreira Nunes  560641 
// Vinicius Kozonoe Guaglini  567264 
// Yan Lucas Gonçalves da Silva  567046 
// Bruno Santos Castilho| 566799 

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>

// ============================================================
// CONFIGURAÇÕES
// ============================================================
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* MQTT_BROKER   = "74.235.202.170";
const int   MQTT_PORT     = 1883;
const char* DEVICE_ID     = "darksky-esp32-001";
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
unsigned long lastSensorRead   = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState  = false;
String ledStatus  = "off";  // "green", "red", "off"

const unsigned long SENSOR_INTERVAL = 5000;
const unsigned long BUZZER_INTERVAL = 300;

// ============================================================
// CALLBACK — recebe comando do Python
// Formato: "darksky-esp32-001@green|" ou "darksky-esp32-001@red|" ou "darksky-esp32-001@off|"
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.print(F("[CMD RX] "));
  Serial.println(msg);

  // Extrai o comando após o @
  int atIndex   = msg.indexOf('@');
  int pipeIndex = msg.indexOf('|');
  if (atIndex != -1 && pipeIndex != -1) {
    String cmd = msg.substring(atIndex + 1, pipeIndex);
    cmd.toLowerCase();
    ledStatus = cmd;
    Serial.print(F("Comando: "));
    Serial.println(cmd);
  }
}

// ============================================================
void connectWiFi() {
  Serial.print(F("Conectando WiFi..."));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(F("Conectando WiFi..."));
  display.display();
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F(" OK"));
  } else {
    Serial.println(F(" FALHOU"));
  }
}

void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  Serial.print(F("Conectando MQTT..."));
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    if (mqtt.connect(DEVICE_ID)) {
      Serial.println(F(" OK"));
      mqtt.subscribe(TOPIC_SUB);
    } else {
      delay(2000); attempts++;
    }
  }
}

void publishRawData(float temp, float hum, float pressure, int ldrRaw) {
  if (!mqtt.connected()) return;
  char payload[100];
  snprintf(payload, sizeof(payload),
           "t|%.1f|h|%.1f|p|%.1f|l|%d",
           temp, hum, pressure, ldrRaw);
  mqtt.publish(TOPIC_PUB, payload);
  Serial.print(F("[TX] ")); Serial.println(payload);
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

  if (!bmp.begin()) Serial.println(F("BMP180: nao encontrado"));

  display.setTextSize(1);
  display.setCursor(20, 20);
  display.println(F("DarkSky Edge"));
  display.setCursor(10, 35);
  display.println(F("Aguardando Python..."));
  display.display();
  delay(1500);

  connectWiFi();
  connectMQTT();
}

// ============================================================
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();

  // ---- Atuadores controlados pelo Python ----
  if (ledStatus == "green") {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED,   LOW);
    noTone(BUZZER_PIN);
  } else if (ledStatus == "red") {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   HIGH);
    if (now - lastBuzzerToggle >= BUZZER_INTERVAL) {
      lastBuzzerToggle = now;
      buzzerState = !buzzerState;
      if (buzzerState) tone(BUZZER_PIN, 500);
      else             noTone(BUZZER_PIN);
    }
  } else {
    // "off" ou moderado — LEDs apagados
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   LOW);
    noTone(BUZZER_PIN);
  }

  // ---- Leitura e publicação dos dados RAW ----
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    float temp     = dht.readTemperature();
    float hum      = dht.readHumidity();
    float pressure = bmp.readPressure() / 100.0F;
    int   ldrRaw   = analogRead(LDR_PIN);

    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("DHT22: falha")); return;
    }

    publishRawData(temp, hum, pressure, ldrRaw);

    // ---- OLED ----
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("=== DarkSky Edge ==="));
    display.setCursor(0, 12);
    display.print(F("Temp: ")); display.print(temp, 1); display.println(F("C"));
    display.setCursor(0, 22);
    display.print(F("Umid: ")); display.print(hum, 1); display.println(F("%"));
    display.setCursor(0, 32);
    display.print(F("Press:")); display.print(pressure, 1); display.println(F("hPa"));
    display.setCursor(0, 42);
    display.print(F("LDR:  ")); display.println(ldrRaw);
    display.setCursor(0, 54);
    if      (ledStatus == "green") display.println(F("CEU: IDEAL ●"));
    else if (ledStatus == "red")   display.println(F("CEU: RUIM ●"));
    else                           display.println(F("CEU: MODERADO"));
    display.display();

    // ---- Serial ----
    Serial.println(F("===== DARKSKY RAW ====="));
    Serial.print(F("Temp:    ")); Serial.println(temp);
    Serial.print(F("Umidade: ")); Serial.println(hum);
    Serial.print(F("Pressao: ")); Serial.println(pressure);
    Serial.print(F("LDR:     ")); Serial.println(ldrRaw);
    Serial.print(F("Status:  ")); Serial.println(ledStatus);
    Serial.println();
  }
}
