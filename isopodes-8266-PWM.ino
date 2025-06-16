#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <EEPROM.h>

#define DHTPIN D6
#define DHTTYPE DHT22
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define FAN_PIN D5
#define EEPROM_ADDR 0
#define EEPROM_SIZE 32

#define DEFAULT_TEMP_TARGET 24.0
#define DEFAULT_HUM_TARGET 55.0
#define DEFAULT_KP_TEMP 1.0
#define DEFAULT_KP_HUM 1.0
#define DEFAULT_WEIGHT 0.5
#define DEFAULT_MANUAL_MODE false
#define DEFAULT_MANUAL_PWM 0

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);

struct Settings {
  float tempTarget;
  float humTarget;
  float kpTemp;
  float kpHum;
  float weight;
  bool manualMode;
  int manualPWM;
} settings;

float temperature, humidity;
int fanPWMPercent = 0;

const char* ssid = "Madame_Lapin_box_4655434B";
const char* password = "Mdpscv|Dmscr!";
IPAddress localIP;

bool isValidNumber(const String& s) {
  char* endptr = nullptr;
  strtod(s.c_str(), &endptr);
  return endptr != s.c_str() && *endptr == '\0';
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_ADDR, settings);
  if (isnan(settings.tempTarget)) {
    settings.tempTarget = DEFAULT_TEMP_TARGET;
    settings.humTarget  = DEFAULT_HUM_TARGET;
    settings.kpTemp     = DEFAULT_KP_TEMP;
    settings.kpHum      = DEFAULT_KP_HUM;
    settings.weight     = DEFAULT_WEIGHT;
    settings.manualMode = DEFAULT_MANUAL_MODE;
    settings.manualPWM  = DEFAULT_MANUAL_PWM;
  }
}

void saveSettings() {
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  dht.begin();
  setupDisplay();
  pinMode(FAN_PIN, OUTPUT);
  analogWriteRange(1023);
  analogWrite(FAN_PIN, 0);
  connectToWiFi();
  setupWebServer();
}

void loop() {
  server.handleClient();
  readSensors();
  computeFanControl();
  updateDisplay();
  delay(1000);
}

void readSensors() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("⚠️ Erreur capteur DHT22");
  }
}

void setupDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED non détecté !");
    while (1);
  }
  display.setRotation(2);
  display.clearDisplay();
  display.display();
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("T: %.1f C\n", temperature);
  display.printf("H: %.1f %%\n", humidity);
  display.printf("FAN: %3d %%\n", fanPWMPercent);
  display.setCursor(0, 24);
  display.print(localIP);
  display.display();
}

void computeFanControl() {
  int pwm;
  if (settings.manualMode) {
    pwm = constrain(settings.manualPWM, 0, 1023);
  } else {
    float tempError = temperature - settings.tempTarget;
    float humError  = humidity - settings.humTarget;
    float normTemp = tempError * settings.kpTemp;
    float normHum  = humError  * settings.kpHum;
    float score = settings.weight * normTemp + (1.0 - settings.weight) * normHum;
    score = constrain(score, -1.0, 1.0);
    pwm = (score > 0) ? int(score * 1023) : 0;
  }
  pwm = constrain(pwm, 0, 1023);
  analogWrite(FAN_PIN, pwm);
  fanPWMPercent = (pwm * 100) / 1023;
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 45) {
    delay(500);
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) localIP = WiFi.localIP();
  else while (true);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/save", handleSave);
  server.begin();
}

void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset='UTF-8'><title>Biosphère</title>
  <style>body{font-family:sans-serif;margin:20px}fieldset{margin-bottom:15px}input[type=number]{width:80px}</style>
  </head><body><h2>Réglages PID et PWM</h2>
  <form action='/set' method='get'>
    <fieldset><legend>Consignes</legend>
      Temp. cible (°C): <input type='number' name='t' value=')rawliteral" + String(settings.tempTarget) + R"rawliteral(' step='0.1' min='0' max='50' required><br>
      Humidité cible (%): <input type='number' name='h' value=')" + String(settings.humTarget) + R"rawliteral(' step='0.1' min='0' max='100' required><br>
    </fieldset>
    <fieldset><legend>Coefficients PID</legend>
      Kp Temp: <input type='number' name='kpt' value=')" + String(settings.kpTemp) + R"rawliteral(' step='0.01' min='0.01' max='10' required><br>
      Kp Hum: <input type='number' name='kph' value=')" + String(settings.kpHum) + R"rawliteral(' step='0.01' min='0.01' max='10' required><br>
    </fieldset>
    <fieldset><legend>Pondération</legend>
      Poids Temp/Hum: <input type='number' name='w' value=')" + String(settings.weight) + R"rawliteral(' step='0.01' min='0' max='1' required><br>
    </fieldset>
    <fieldset><legend>Ventilateur</legend>
      Mode manuel: <input type='checkbox' name='manual' )" + (settings.manualMode ? "checked" : "") + R"rawliteral(><br>
      PWM manuelle: <input type='number' name='pwm' value=')" + String(settings.manualPWM) + R"rawliteral(' step='1' min='0' max='1023'><br>
    </fieldset>
    <input type='submit' value='Appliquer'>
  </form>
  <form action='/save'><input type='submit' value='💾 Sauvegarder'></form><hr>
  <h2>État</h2><ul>
    <li>IP: )" + WiFi.localIP().toString() + R"rawliteral(</li>
    <li>SSID: )" + String(WiFi.SSID()) + R"rawliteral(</li>
    <li>RSSI: )" + String(WiFi.RSSI()) + R"rawliteral( dBm</li>
    <li>Température: )" + String(temperature, 1) + R"rawliteral( °C</li>
    <li>Humidité: )" + String(humidity, 1) + R"rawliteral( %</li>
    <li>Ventilateur: )" + String(fanPWMPercent) + R"rawliteral( %</li>
    <li>Mode: )" + (settings.manualMode ? "Manuel" : "Auto") + R"rawliteral(</li>
  </ul>
  </body></html>")rawliteral";
  server.send(200, "text/html", html);
}

void handleSet() {
  bool error = false;
  if (server.hasArg("t") && isValidNumber(server.arg("t")))
    settings.tempTarget = constrain(server.arg("t").toFloat(), 0, 50);
  else error = true;

  if (server.hasArg("h") && isValidNumber(server.arg("h")))
    settings.humTarget = constrain(server.arg("h").toFloat(), 0, 100);
  else error = true;

  if (server.hasArg("kpt") && isValidNumber(server.arg("kpt")))
    settings.kpTemp = constrain(server.arg("kpt").toFloat(), 0.01, 10);
  else error = true;

  if (server.hasArg("kph") && isValidNumber(server.arg("kph")))
    settings.kpHum = constrain(server.arg("kph").toFloat(), 0.01, 10);
  else error = true;

  if (server.hasArg("w") && isValidNumber(server.arg("w")))
    settings.weight = constrain(server.arg("w").toFloat(), 0.0, 1.0);
  else error = true;

  settings.manualMode = server.hasArg("manual");
  if (server.hasArg("pwm") && isValidNumber(server.arg("pwm")))
    settings.manualPWM = constrain(server.arg("pwm").toInt(), 0, 1023);

  if (error) {
    server.send(400, "text/html", "<h3>Entrée invalide. Merci de vérifier les champs.</h3><a href='/'>Retour</a>");
  } else {
    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void handleSave() {
  saveSettings();
  server.sendHeader("Location", "/");
  server.send(303);
}
