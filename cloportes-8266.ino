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
#define DEFAULT_FAN_MODE 0
#define DEFAULT_TEMP_MARGIN 2.0
#define DEFAULT_HUM_MARGIN 2.0
#define DEFAULT_ONOFF_LOGIC 2

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
  int fanMode;      // 0=On/Off, 1=PWM
  float tempMargin;
  float humMargin;
  int onOffLogic;   // 0=H, 1=T, 2=H+T
} settings;

float temperature, humidity;
int fanPWMPercent = 0;

const char* ssid = "SSID_WIFI";
const char* password = "PASS_WIFI";
IPAddress localIP;

// pour pas se faire chier
bool isValidNumber(const String& s);
void loadSettings();
void saveSettings();
void clearSettings();
void readSensors();
void setupDisplay();
void updateDisplay();
void updateWiFiProgress(int attempts, int maxAttempts);
void computeFanControl();
void connectToWiFi();
void setupWebServer();
void handleRoot();
void handleSet();
void handleSave();
void handleClear();

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
    settings.fanMode    = DEFAULT_FAN_MODE;
    settings.tempMargin = DEFAULT_TEMP_MARGIN;
    settings.humMargin  = DEFAULT_HUM_MARGIN;
    settings.onOffLogic = DEFAULT_ONOFF_LOGIC;
  }
}

void saveSettings() {
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
  Serial.println("💾 Paramètres sauvegardés en EEPROM");
}

void clearSettings() {
  // Remettre les valeurs par défaut
  settings.tempTarget = DEFAULT_TEMP_TARGET;
  settings.humTarget  = DEFAULT_HUM_TARGET;
  settings.kpTemp     = DEFAULT_KP_TEMP;
  settings.kpHum      = DEFAULT_KP_HUM;
  settings.weight     = DEFAULT_WEIGHT;
  settings.manualMode = DEFAULT_MANUAL_MODE;
  settings.manualPWM  = DEFAULT_MANUAL_PWM;
  settings.fanMode    = DEFAULT_FAN_MODE;
  settings.tempMargin = DEFAULT_TEMP_MARGIN;
  settings.humMargin  = DEFAULT_HUM_MARGIN;
  settings.onOffLogic = DEFAULT_ONOFF_LOGIC;
  
  // Sauvegarder les valeurs par défaut en EEPROM
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
  Serial.println("🗑️ EEPROM effacée, paramètres par défaut restaurés");
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

void updateWiFiProgress(int attempts, int maxAttempts) {
  // Mise à jour série - points classiques
  Serial.print(".");
  if (attempts % 10 == 0) {
    Serial.print(" ");
    Serial.print(attempts);
    Serial.print("/");
    Serial.println(maxAttempts);
  }
  
  // Mise à jour OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connexion WiFi...");
  
  // Barre de progression graphique plus compacte
  int barWidth = 100;
  int barHeight = 6;
  int barX = (SCREEN_WIDTH - barWidth) / 2;
  int barY = 12;
  
  // Cadre de la barre
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  
  // Remplissage de la barre
  int fillWidth = (attempts * (barWidth - 2)) / maxAttempts;
  if (fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
  }
  
  // Pourcentage plus compact
  int progress = (attempts * 100) / maxAttempts;
  display.setCursor(barX + barWidth/2 - 9, barY + barHeight + 1);
  display.print(progress);
  display.print("%");
  
  display.display();
}

void computeFanControl() {
  int pwm;
  if (settings.manualMode) {
    pwm = constrain(settings.manualPWM, 0, 1023);
  } else if (settings.fanMode == 1) {
    // Mode PWM (ancien comportement)
    float tempError = temperature - settings.tempTarget;
    float humError  = humidity - settings.humTarget;
    float normTemp = tempError * settings.kpTemp;
    float normHum  = humError  * settings.kpHum;
    float score = settings.weight * normTemp + (1.0 - settings.weight) * normHum;
    score = constrain(score, -1.0, 1.0);
    pwm = (score > 0) ? int(score * 1023) : 0;
  } else {
    // Mode On/Off avec marges
    bool tempTrigger = (temperature > settings.tempTarget + settings.tempMargin);
    bool humTrigger = (humidity > settings.humTarget + settings.humMargin);
    bool fanOn = false;
    
    switch (settings.onOffLogic) {
      case 0: // H seulement
        fanOn = humTrigger;
        break;
      case 1: // T seulement
        fanOn = tempTrigger;
        break;
      case 2: // H+T (porte ET)
        fanOn = tempTrigger && humTrigger;
        break;
    }
    
    pwm = fanOn ? 1023 : 0;
  }
  
  pwm = constrain(pwm, 0, 1023);
  analogWrite(FAN_PIN, pwm);
  fanPWMPercent = (pwm * 100) / 1023;
}

void connectToWiFi() {
  Serial.println("🚀 Démarrage connexion WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  int maxAttempts = 45;
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    attempts++;
    updateWiFiProgress(attempts, maxAttempts);
    delay(500);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP();
    Serial.println("✅ WiFi connecté !");
    Serial.print("📍 IP: ");
    Serial.println(localIP);
    
    // Affichage succès sur OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("WiFi connecte!");
    display.setCursor(0, 10);
    display.print("IP: ");
    display.println(localIP);
    display.display();
    delay(2000);
  } else {
    Serial.println("❌ Échec connexion WiFi");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 8);
    display.println("Echec WiFi!");
    display.display();
    while (true);
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/save", handleSave);
  server.on("/clear", handleClear);
  server.begin();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Biosphère</title>";
  html += "<style>body{font-family:sans-serif;margin:20px}fieldset{margin-bottom:15px}input[type=number],select{width:80px}</style>";
  html += "</head><body><h2>Réglages PID et PWM</h2>";
  html += "<form action='/set' method='get'>";
  
  html += "<fieldset><legend>Consignes</legend>";
  html += "Temp. cible (°C): <input type='number' name='t' value='" + String(settings.tempTarget) + "' step='0.1' min='0' max='50' required><br>";
  html += "Humidité cible (%): <input type='number' name='h' value='" + String(settings.humTarget) + "' step='0.1' min='0' max='100' required><br>";
  html += "</fieldset>";
  
  html += "<fieldset><legend>Mode Ventilateur</legend>";
  html += "Mode: <select name='fanmode'>";
  html += "<option value='0'" + String(settings.fanMode == 0 ? " selected" : "") + ">On/Off</option>";
  html += "<option value='1'" + String(settings.fanMode == 1 ? " selected" : "") + ">PWM</option>";
  html += "</select><br></fieldset>";
  
  // Paramètres PWM (affiché seulement si mode PWM)
  html += "<fieldset id='pwm-params'><legend>Paramètres PWM</legend>";
  html += "Kp Temp: <input type='number' name='kpt' value='" + String(settings.kpTemp) + "' step='0.01' min='0.01' max='10' required><br>";
  html += "Kp Hum: <input type='number' name='kph' value='" + String(settings.kpHum) + "' step='0.01' min='0.01' max='10' required><br>";
  html += "Poids Temp/Hum: <input type='number' name='w' value='" + String(settings.weight) + "' step='0.01' min='0' max='1' required><br>";
  html += "</fieldset>";
  
  // Paramètres On/Off (affiché seulement si mode On/Off)
  html += "<fieldset id='onoff-params'><legend>Paramètres On/Off</legend>";
  html += "Marge Temp (°C): <input type='number' name='tmargin' value='" + String(settings.tempMargin) + "' step='0.1' min='0.1' max='10' required><br>";
  html += "Marge Hum (%): <input type='number' name='hmargin' value='" + String(settings.humMargin) + "' step='0.1' min='0.1' max='20' required><br>";
  html += "Logique: <select name='logic'>";
  html += "<option value='0'" + String(settings.onOffLogic == 0 ? " selected" : "") + ">Humidité seulement</option>";
  html += "<option value='1'" + String(settings.onOffLogic == 1 ? " selected" : "") + ">Température seulement</option>";
  html += "<option value='2'" + String(settings.onOffLogic == 2 ? " selected" : "") + ">Temp ET Humidité</option>";
  html += "</select><br></fieldset>";
  
  html += "<fieldset><legend>Contrôle Manuel</legend>";
  html += "Mode manuel: <input type='checkbox' name='manual' " + String(settings.manualMode ? "checked" : "") + "><br>";
  html += "PWM manuelle: <input type='number' name='pwm' value='" + String(settings.manualPWM) + "' step='1' min='0' max='1023'><br>";
  html += "</fieldset>";
  
  html += "<script>";
  html += "function toggleParams() {";
  html += "  var fanMode = document.querySelector('select[name=\"fanmode\"]').value;";
  html += "  document.getElementById('pwm-params').style.display = fanMode == '1' ? 'block' : 'none';";
  html += "  document.getElementById('onoff-params').style.display = fanMode == '0' ? 'block' : 'none';";
  html += "}";
  html += "document.querySelector('select[name=\"fanmode\"]').addEventListener('change', toggleParams);";
  html += "toggleParams();";
  html += "</script>";
  
  html += "<input type='submit' value='Appliquer'>";
  html += "</form>";
  html += "<form action='/save'><input type='submit' value='💾 Sauvegarder'></form>";
  html += "<form action='/clear' onsubmit='return confirm(\"Êtes-vous sûr de vouloir effacer tous les paramètres ?\");'><input type='submit' value='🗑️ Reset EEPROM'></form><hr>";
  html += "<h2>État</h2><ul>";
  html += "<li>IP: " + WiFi.localIP().toString() + "</li>";
  html += "<li>SSID: " + String(WiFi.SSID()) + "</li>";
  html += "<li>RSSI: " + String(WiFi.RSSI()) + " dBm</li>";
  html += "<li>Température: " + String(temperature, 1) + " °C</li>";
  html += "<li>Humidité: " + String(humidity, 1) + " %</li>";
  html += "<li>Ventilateur: " + String(fanPWMPercent) + " %</li>";
  html += "<li>Mode: " + String(settings.manualMode ? "Manuel" : (settings.fanMode == 1 ? "PWM Auto" : "On/Off Auto")) + "</li>";
  html += "</ul></body></html>";
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

  if (server.hasArg("fanmode") && isValidNumber(server.arg("fanmode")))
    settings.fanMode = constrain(server.arg("fanmode").toInt(), 0, 1);

  if (server.hasArg("kpt") && isValidNumber(server.arg("kpt")))
    settings.kpTemp = constrain(server.arg("kpt").toFloat(), 0.01, 10);

  if (server.hasArg("kph") && isValidNumber(server.arg("kph")))
    settings.kpHum = constrain(server.arg("kph").toFloat(), 0.01, 10);

  if (server.hasArg("w") && isValidNumber(server.arg("w")))
    settings.weight = constrain(server.arg("w").toFloat(), 0.0, 1.0);

  if (server.hasArg("tmargin") && isValidNumber(server.arg("tmargin")))
    settings.tempMargin = constrain(server.arg("tmargin").toFloat(), 0.1, 10);

  if (server.hasArg("hmargin") && isValidNumber(server.arg("hmargin")))
    settings.humMargin = constrain(server.arg("hmargin").toFloat(), 0.1, 20);

  if (server.hasArg("logic") && isValidNumber(server.arg("logic")))
    settings.onOffLogic = constrain(server.arg("logic").toInt(), 0, 2);

  settings.manualMode = server.hasArg("manual");
  if (server.hasArg("pwm") && isValidNumber(server.arg("pwm")))
    settings.manualPWM = constrain(server.arg("pwm").toInt(), 0, 1023);

  // Sauvegarder automatiquement tous les changements en EEPROM
  saveSettings();

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

void handleClear() {
  clearSettings();
  server.sendHeader("Location", "/");
  server.send(303);
}
