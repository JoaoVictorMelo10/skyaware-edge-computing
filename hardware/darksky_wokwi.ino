// ============================================================
//  DarkSky Edge — Protótipo Wokwi (sem WiFi/MQTT)
//  Sensores: DHT22, LDR, BMP180
//  Atuadores: OLED SSD1306, LED Verde, LED Vermelho, Buzzer
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- DHT22 ----------
#define DHTPIN   15
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------- LDR ----------
#define LDR_PIN  34

// ---------- BMP180 ----------
Adafruit_BMP085 bmp;

// ---------- LEDs ----------
#define GREEN_LED  18
#define RED_LED    19

// ---------- BUZZER ----------
#define BUZZER_PIN 27

// ---------- Controle de tempo ----------
unsigned long lastSensorRead   = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;
const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long BUZZER_INTERVAL = 300;

// ---------- Score orbital (placeholder — virá do FIWARE via MQTT) ----------
// Valor neutro (5.0) para que os sensores consigam puxar o score
// tanto para cima (LED verde) quanto para baixo (LED vermelho)
float orbitalQuality = 5.0;

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

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED: falha na inicializacao"));
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  // BMP180
  if (!bmp.begin()) {
    Serial.println(F("BMP180: sensor nao encontrado!"));
  }

  // Tela de boot
  display.setTextSize(1);
  display.setCursor(20, 20);
  display.println(F("DarkSky Edge"));
  display.setCursor(25, 35);
  display.println(F("Iniciando..."));
  display.display();
  delay(1500);
}

// ============================================================
void loop() {
  unsigned long now = millis();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("DHT22: falha na leitura"));
      return;
    }

    int   ldrRaw   = analogRead(LDR_PIN);
    float pressure = bmp.readPressure() / 100.0F; // hPa

    // ---- Fator 1: Escuridão (LDR) ----
    // LDR alto = muito luz = ruim para observação
    float darkness = (float)(4095 - ldrRaw) / 4095.0F * 10.0F;

    // ---- Fator 2: Umidade (DHT22) ----
    // Umidade alta = turbulência atmosférica = ruim
    float humFactor = constrain(10.0F - (hum / 10.0F), 0.0F, 10.0F);

    // ---- Fator 3: Pressão (BMP180) ----
    // Pressão alta (>1013) = tempo estável = bom para observação
    // Pressão baixa (<1000) = frente fria = ruim
    // Mapeia 980–1030 hPa → 0–10
    float pressureFactor = constrain((pressure - 980.0F) / 5.0F, 0.0F, 10.0F);

    // ---- Sky Observation Score ----
    // Pesos: orbital 40% | escuridão 30% | umidade 20% | pressão 10%
    float skyScore = (orbitalQuality * 0.4F)
                   + (darkness       * 0.3F)
                   + (humFactor      * 0.2F)
                   + (pressureFactor * 0.1F);

    skyScore = constrain(skyScore, 0.0F, 10.0F);

    // ---- Atuadores ----
    if (skyScore >= 7.0F) {
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

    // ---- Display OLED ----
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println(F("=== DarkSky Edge ==="));

    display.setCursor(0, 12);
    display.print(F("Temp: "));
    display.print(temp, 1);
    display.println(F(" C"));

    display.setCursor(0, 22);
    display.print(F("Umid: "));
    display.print(hum, 1);
    display.println(F(" %"));

    display.setCursor(0, 32);
    display.print(F("Press: "));
    display.print(pressure, 1);
    display.println(F("hPa"));

    display.setCursor(0, 42);
    display.print(F("Luz: "));
    display.println(ldrRaw);

    display.setCursor(0, 54);
    display.print(F("Score: "));
    display.print(skyScore, 1);
    if      (skyScore >= 7.0F) display.println(F(" IDEAL"));
    else if (skyScore >= 4.0F) display.println(F(" MOD"));
    else                       display.println(F(" RUIM"));

    display.display();

    // ---- Serial ----
    Serial.println(F("===== DARKSKY ====="));
    Serial.print(F("Temp:        ")); Serial.print(temp);           Serial.println(F(" C"));
    Serial.print(F("Umidade:     ")); Serial.print(hum);            Serial.println(F(" %"));
    Serial.print(F("Pressao:     ")); Serial.print(pressure);       Serial.println(F(" hPa"));
    Serial.print(F("LDR Raw:     ")); Serial.println(ldrRaw);
    Serial.print(F("Darkness:    ")); Serial.println(darkness);
    Serial.print(F("HumFator:    ")); Serial.println(humFactor);
    Serial.print(F("PressFator:  ")); Serial.println(pressureFactor);
    Serial.print(F("OrbitalQ:    ")); Serial.println(orbitalQuality);
    Serial.print(F("Sky Score:   ")); Serial.println(skyScore);
    Serial.println();
  }
}
