#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

// Configuration matérielle
#define DHTPIN D6
#define DHTTYPE DHT22
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define FAN_PIN D5
#define EEPROM_ADDR 0
#define EEPROM_SIZE 64

// Valeurs par défaut
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

// Timing optimisé
#define SENSOR_READ_INTERVAL 2000
#define DISPLAY_UPDATE_INTERVAL 1000
#define WEB_CHECK_INTERVAL 100
#define WIFI_TIMEOUT 30
#define MAX_RETRY_SENSOR 3
#define WIFI_RETRY_INTERVAL 180000  // 3 minutes en millisecondes

// WiFi - à modifier selon vos besoins
const char* ssid = "SSID_WIFI";
const char* password = "PASS_WIFI";
const char* hostname = "isobox";
//===============do not edit:
unsigned long lastWifiRetry = 0;
bool wifiConnected = false;

struct Settings {
  uint8_t version;
  float tempTarget, humTarget, kpTemp, kpHum, weight, tempMargin, humMargin;
  bool manualMode;
  int manualPWM, fanMode, onOffLogic;
  uint32_t checksum;
} settings;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);

float temperature = NAN, humidity = NAN;
int fanPWMPercent = 0;
IPAddress localIP;
unsigned long lastSensorRead = 0, lastDisplayUpdate = 0, lastWebCheck = 0;
bool sensorError = false;
uint8_t sensorRetryCount = 0, bootStep = 0;

bool isValidNumber(const String& s) {
  if (s.length() == 0 || s.length() > 15) return false; // Augmenté pour les nombres négatifs
  bool hasDecimal = false, hasDigit = false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (i == 0 && (c == '-' || c == '+')) continue;
    if (c == '.' && !hasDecimal) { hasDecimal = true; continue; }
    if (isdigit(c)) { hasDigit = true; continue; }
    return false;
  }
  return hasDigit; // Au moins un chiffre requis
}

uint32_t calculateChecksum(const Settings& s) {
  uint32_t checksum = 0;
  const uint8_t* data = (const uint8_t*)&s;
  for (size_t i = 0; i < sizeof(Settings) - sizeof(uint32_t); i++) {
    checksum = ((checksum << 1) | (checksum >> 31)) ^ data[i];
  }
  return checksum;
}

bool validateSettings(const Settings& s) {
  return (s.version == 1 && s.tempTarget >= 0 && s.tempTarget <= 50 &&
          s.humTarget >= 0 && s.humTarget <= 100 && s.kpTemp >= 0.01 && s.kpTemp <= 10 &&
          s.kpHum >= 0.01 && s.kpHum <= 10 && s.weight >= 0 && s.weight <= 1 &&
          s.manualPWM >= 0 && s.manualPWM <= 1023 && s.fanMode >= 0 && s.fanMode <= 1 &&
          s.tempMargin >= 0.1 && s.tempMargin <= 10 && s.humMargin >= 0.1 && s.humMargin <= 20 &&
          s.onOffLogic >= 0 && s.onOffLogic <= 2);
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_ADDR, settings);
  if (settings.checksum != calculateChecksum(settings) || !validateSettings(settings)) {
    Serial.println("⚠️ Paramètres corrompus, restauration");
    clearSettings();
    return;
  }
  Serial.println("✅ Paramètres chargés");
}

void saveSettings() {
  settings.version = 1;
  settings.checksum = calculateChecksum(settings);
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
  Serial.println("💾 Sauvegardé");
}

void clearSettings() {
  settings = {1, DEFAULT_TEMP_TARGET, DEFAULT_HUM_TARGET, DEFAULT_KP_TEMP, DEFAULT_KP_HUM, 
              DEFAULT_WEIGHT, DEFAULT_TEMP_MARGIN, DEFAULT_HUM_MARGIN, DEFAULT_MANUAL_MODE, 
              DEFAULT_MANUAL_PWM, DEFAULT_FAN_MODE, DEFAULT_ONOFF_LOGIC, 0};
  settings.checksum = calculateChecksum(settings);
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
  Serial.println("🗑️ Reset effectué");
}

void showBootStep(const String& msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("IsoBox v2.0");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 14);
  display.println(msg);
  
  // Barre de progression
  int progress = (bootStep * 128) / 6;
  display.drawRect(0, 26, 128, 6, SSD1306_WHITE);
  if (progress > 0) display.fillRect(1, 27, progress-1, 4, SSD1306_WHITE);
  
  display.display();
  bootStep++;
  delay(800);
}

void setup() {
  Serial.begin(115200);
  Serial.println("🦐 IsoBox v2.0 - Démarrage");
  
  // Init display
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.setRotation(2);
    showBootStep("Init OLED...");
  }
  
  showBootStep("Config WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  
  showBootStep("Charge config...");
  loadSettings();
  
  showBootStep("Init DHT22...");
  dht.begin();
  delay(2000); // Attendre que le DHT22 soit prêt
  readSensors();
  showBootStep("Init ventilo...");
  pinMode(FAN_PIN, OUTPUT);
  analogWriteRange(1023);
  analogWrite(FAN_PIN, 0);
  
  showBootStep("Connexion WiFi...");
  if (connectToWiFi()) {
    setupWebServer();
    if (MDNS.begin(hostname)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("✅ mDNS: http://" + String(hostname) + ".local");
    }
  }
  
  showBootStep("Pret!");
  readSensors();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Gestion de la reconnexion WiFi toutes les 3 minutes en mode autonome
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastWifiRetry >= WIFI_RETRY_INTERVAL) {
      Serial.println("⚠️ Tentative de reconnexion WiFi...");
      wifiConnected = connectToWiFi();
      lastWifiRetry = currentTime;
      
      // Si reconnexion réussie, réinitialiser le serveur web
      if (wifiConnected) {
        setupWebServer();
        if (MDNS.begin(hostname)) {
          MDNS.addService("http", "tcp", 80);
          Serial.println("✅ mDNS: http://" + String(hostname) + ".local");
        }
      }
    }
  }
  
  // Gestion du serveur web seulement si WiFi connecté
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    if (currentTime - lastWebCheck >= WEB_CHECK_INTERVAL) {
      server.handleClient();
      MDNS.update();
      lastWebCheck = currentTime;
    }
  } else {
    wifiConnected = false; // Marquer comme déconnecté
  }
  
  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
    readSensors();
    computeFanControl();
    lastSensorRead = currentTime;
  }
  
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = currentTime;
  }
  
  delay(10);
}

void readSensors() {
  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();
  
  // Vérification des valeurs aberrantes en plus de NaN
  if (isnan(newHumidity) || isnan(newTemperature) || 
      newHumidity < 0 || newHumidity > 100 ||
      newTemperature < -40 || newTemperature > 80) {
    sensorRetryCount++;
    if (sensorRetryCount >= MAX_RETRY_SENSOR) {
      sensorError = true;
      Serial.printf("⚠️ Erreur DHT22 (retry: %d)\n", sensorRetryCount);
    }
    return;
  }
  
  // Filtre anti-bruit simple - CORRECTION ICI
  if (!sensorError && !isnan(humidity) && !isnan(temperature)) {
    // Filtre seulement si les valeurs précédentes sont valides
    humidity = (humidity * 0.8) + (newHumidity * 0.2);
    temperature = (temperature * 0.8) + (newTemperature * 0.2);
  } else {
    // Première lecture ou après erreur - utiliser directement les nouvelles valeurs
    humidity = newHumidity;
    temperature = newTemperature;
  }
  
  sensorError = false;
  sensorRetryCount = 0;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  if (sensorError || isnan(temperature) || isnan(humidity)) {
    display.println("ERREUR CAPTEUR!");
    display.printf("Retry: %d/%d", sensorRetryCount, MAX_RETRY_SENSOR);
  } else {
    // Affichage sécurisé avec limitation des décimales
    float dispTemp = constrain(temperature, -99.9, 99.9);
    float dispHum = constrain(humidity, 0.0, 99.9);
    
    display.printf("T:%4.1fC %c %4.1fC\n", dispTemp, 
                   (dispTemp > settings.tempTarget + 0.5) ? '>' : 
                   (dispTemp < settings.tempTarget - 0.5) ? '<' : '=', 
                   settings.tempTarget);
    display.printf("H:%4.1f%% %c %4.1f%%\n", dispHum,
                   (dispHum > settings.humTarget + 2) ? '>' : 
                   (dispHum < settings.humTarget - 2) ? '<' : '=',
                   settings.humTarget);
  }
  
  display.printf("FAN:%3d%% %s\n", fanPWMPercent, 
                 settings.manualMode ? "MAN" : (settings.fanMode ? "PWM" : "O/O"));
  display.setCursor(0, 24);
  
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    display.print(localIP);
  } else {
    display.print("Mode autonome");
  }
  
  display.display();
}


// Fonction utilitaire pour créer un défilement fluide du SSID
String getScrollingSSID(const String& fullSSID, int maxWidth = 18) {
  if (fullSSID.length() <= maxWidth) {
    return fullSSID; // Pas besoin de défilement
  }
  
  // Paramètres de défilement
  const int scrollSpeed = 300; // Millisecondes par caractère
  const int pauseDuration = 2000; // Pause en millisecondes au début et à la fin
  const int totalScrollChars = fullSSID.length() - maxWidth + 1;
  const int totalCycleTime = pauseDuration + (totalScrollChars * scrollSpeed) + pauseDuration;
  
  int cyclePosition = (millis() % totalCycleTime);
  
  if (cyclePosition < pauseDuration) {
    // Pause au début - afficher le début du SSID
    return fullSSID.substring(0, maxWidth);
  } else if (cyclePosition >= pauseDuration + (totalScrollChars * scrollSpeed)) {
    // Pause à la fin - afficher la fin du SSID
    return fullSSID.substring(fullSSID.length() - maxWidth);
  } else {
    // Phase de défilement
    int scrollPos = (cyclePosition - pauseDuration) / scrollSpeed;
    scrollPos = constrain(scrollPos, 0, totalScrollChars - 1);
    return fullSSID.substring(scrollPos, scrollPos + maxWidth);
  }
}

void updateWiFiProgress(int attempts, int maxAttempts) {
  Serial.print(".");
  if (attempts % 10 == 0) Serial.printf(" %d/%d\n", attempts, maxAttempts);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connexion WiFi...");
  
  // Utilisation de la fonction de défilement améliorée
  String displaySSID = getScrollingSSID(String(ssid), 18);
  display.printf("SSID: %s", displaySSID.c_str());
  
  // Barre de progression
  int barWidth = 100, barHeight = 6, barX = 14, barY = 20;
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  
  int fillWidth = (attempts * (barWidth - 2)) / maxAttempts;
  if (fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
  }
  
  // Pourcentage centré
  display.setCursor(barX + barWidth/2 - 9, barY + barHeight + 1);
  display.printf("%d%%", (attempts * 100) / maxAttempts);
  display.display();
}

void computeFanControl() {
  static int lastPwm = 0;
  static unsigned long lastChange = 0;
  const unsigned long MIN_CHANGE_INTERVAL = 5000; // 5 secondes minimum entre changements
  
  if (sensorError || isnan(temperature) || isnan(humidity)) {
    analogWrite(FAN_PIN, 0);
    fanPWMPercent = 0;
    lastPwm = 0;
    return;
  }
  
  int pwm = 0;
  
  if (settings.manualMode) {
    pwm = constrain(settings.manualPWM, 0, 1023);
  } else if (settings.fanMode == 1) {
    // Mode PWM avec hystérésis
    float tempError = temperature - settings.tempTarget;
    float humError = humidity - settings.humTarget;
    float normTemp = constrain(tempError * settings.kpTemp, -1.0, 1.0);
    float normHum = constrain(humError * settings.kpHum, -1.0, 1.0);
    float score = settings.weight * normTemp + (1.0 - settings.weight) * normHum;
    
    if (score > 0.05) { // Seuil minimal pour éviter oscillations
      pwm = int(constrain(score, 0.0, 1.0) * 1023);
    } else {
      pwm = 0;
    }
  } else {
    // Mode On/Off avec hystérésis
    bool tempTrigger = (temperature > settings.tempTarget + settings.tempMargin);
    bool tempOff = (temperature < settings.tempTarget + settings.tempMargin - 1.0); // Hystérésis 1°C
    bool humTrigger = (humidity > settings.humTarget + settings.humMargin);
    bool humOff = (humidity < settings.humTarget + settings.humMargin - 5.0); // Hystérésis 5%
    
    bool fanOn = false;
    if (settings.onOffLogic == 0) { // Humidité seule
      fanOn = lastPwm > 0 ? !humOff : humTrigger;
    } else if (settings.onOffLogic == 1) { // Température seule  
      fanOn = lastPwm > 0 ? !tempOff : tempTrigger;
    } else { // Temp ET Humidité
      fanOn = lastPwm > 0 ? !(tempOff && humOff) : (tempTrigger && humTrigger);
    }
    
    pwm = fanOn ? 1023 : 0;
  }
  
  // Anti-oscillation: délai minimum entre changements significatifs
  if (abs(pwm - lastPwm) > 50 && (millis() - lastChange) < MIN_CHANGE_INTERVAL) {
    pwm = lastPwm; // Maintenir la valeur précédente
  } else if (pwm != lastPwm) {
    lastChange = millis();
  }
  
  pwm = constrain(pwm, 0, 1023);
  analogWrite(FAN_PIN, pwm);
  fanPWMPercent = (pwm * 100) / 1023;
  lastPwm = pwm;
}

bool connectToWiFi() {
  Serial.println("🚀 Connexion WiFi...");
  
  // Déconnexion propre avant reconnexion
  WiFi.disconnect(true);
  delay(100);
  
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  
  int attempts = 0;
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_TIMEOUT) {
    // Timeout additionnel basé sur le temps réel
    if (millis() - startTime > (WIFI_TIMEOUT * 1000)) break;
    
    attempts++;
    updateWiFiProgress(attempts, WIFI_TIMEOUT);
    delay(500);
    
    // Redémarrage de la connexion si bloquée
    if (attempts % 20 == 0) {
      Serial.println("\n🔄 Relance WiFi...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP();
    wifiConnected = true;
    Serial.printf("✅ WiFi OK! IP: %s, RSSI: %ddBm\n", 
                  localIP.toString().c_str(), WiFi.RSSI());
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 8);
    display.println("WiFi connecte!");
    display.printf("IP: %s", localIP.toString().c_str());
    display.display();
    delay(2000);
    return true;
  } else {
    wifiConnected = false;
    Serial.printf("❌ WiFi échec après %ds - Mode autonome\n", attempts/2);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 12);
    display.println("Mode autonome");
    display.display();
    delay(1000);
    return false;
  }
}


String escapeHtml(const String& input) {
  String output = input;
  output.replace("&", "&amp;");
  output.replace("<", "&lt;");
  output.replace(">", "&gt;");
  output.replace("\"", "&quot;");
  output.replace("'", "&#x27;");
  return output;
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/save", HTTP_GET, handleSave);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("✅ Serveur web OK");
}

String getHTML() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>🦐 IsoBox v2.0</title><style>");
  html += F("body{font-family:Arial,sans-serif;margin:20px;background:#f0f8ff}");
  html += F(".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 8px rgba(0,0,0,0.15)}");
  html += F(".status{background:linear-gradient(135deg,#e8f5e8,#d4f4d4);padding:20px;border-radius:8px;margin:15px 0;border-left:4px solid #28a745}");
  html += F(".sensor-values{display:flex;justify-content:space-around;margin:15px 0}");
  html += F(".sensor-box{background:#fff;border:2px solid;padding:15px;border-radius:8px;text-align:center;min-width:120px}");
  html += F(".temp-box{border-color:#ff6b6b;background:linear-gradient(135deg,#ffe0e0,#fff)}");
  html += F(".hum-box{border-color:#4dabf7;background:linear-gradient(135deg,#e0f0ff,#fff)}");
  html += F(".big-value{font-size:24px;font-weight:bold;margin:5px 0}");
  html += F(".target{font-size:14px;color:#666}");
  html += F("fieldset{margin:15px 0;padding:15px;border:2px solid #ddd;border-radius:8px;background:#fafafa}");
  html += F("legend{font-weight:bold;color:#333;background:white;padding:0 10px}");
  html += F("input,select{padding:8px;margin:5px;border:1px solid #ccc;border-radius:4px}");
  html += F("input[type=number],select{width:90px}");
  html += F("input[type=submit]{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:6px;cursor:pointer;font-size:16px}");
  html += F("input[type=submit]:hover{background:#0056b3;transform:translateY(-1px)}");
  html += F(".btn-save{background:#28a745} .btn-save:hover{background:#1e7e34}");
  html += F(".btn-reset{background:#dc3545} .btn-reset:hover{background:#c82333}");
  html += F(".error{background:#f8d7da;color:#721c24;padding:10px;border-radius:4px;margin:10px 0}");
  html += F("</style>");
  html += F("<script>setInterval(()=>fetch('/status').then(r=>r.json()).then(d=>{");
  html += F("document.getElementById('temp-val').innerText=d.temp+'°C';");
  html += F("document.getElementById('hum-val').innerText=d.hum+'%';");
  html += F("document.getElementById('fan-val').innerText='Ventilateur: '+d.fan+'%';");
  html += F("document.getElementById('uptime').innerText='Uptime: '+d.uptime+'s';}),3000);</script>");
  html += F("</head><body><div class='container'><h1>🦐 IsoBox v2.0</h1>");
  
  html += F("<div class='status'><h3>État du système</h3>");
  html += F("<div class='sensor-values'>");
  html += F("<div class='sensor-box temp-box'><div>Température</div>");
  html += "<div class='big-value' id='temp-val'>" + String(sensorError ? "ERR" : String(temperature, 1) + "°C") + "</div>";
  html += "<div class='target'>Cible: " + String(settings.tempTarget, 1) + "°C</div></div>";
  html += F("<div class='sensor-box hum-box'><div>Humidité</div>");
  html += "<div class='big-value' id='hum-val'>" + String(sensorError ? "ERR" : String(humidity, 1) + "%") + "</div>";
  html += "<div class='target'>Cible: " + String(settings.humTarget, 1) + "%</div></div></div>";
  
  html += "<ul><li>IP: " + WiFi.localIP().toString() + " | Signal: " + String(WiFi.RSSI()) + "dBm</li>";
  html += "<li id='uptime'>Uptime: " + String(millis() / 1000) + "s</li>";
  html += "<li id='fan-val'>Ventilateur: " + String(fanPWMPercent) + "%</li>";
  html += "<li>Mode: " + String(settings.manualMode ? "Manuel" : (settings.fanMode == 1 ? "PWM Auto" : "On/Off Auto")) + "</li>";
  if (sensorError) html += F("<li class='error'>⚠️ Erreur capteur DHT22</li>");
  html += F("</ul></div>");
  
  return html;
}

void handleRoot() {
  String html = getHTML();
  
  html += F("<form method='get' action='/set'>");
  html += F("<fieldset><legend>🎯 Consignes</legend>");
  html += "Température cible (°C): <input type='number' name='t' value='" + String(settings.tempTarget, 1) + "' step='0.1' min='0' max='50' required><br>";
  html += "Humidité cible (%): <input type='number' name='h' value='" + String(settings.humTarget, 1) + "' step='0.1' min='0' max='100' required><br>";
  html += F("</fieldset>");
  
  html += F("<fieldset><legend>⚙️ Mode Ventilateur</legend>");
  html += F("Mode: <select name='fanmode' onchange='toggleParams()'>");
  html += "<option value='0'" + String(settings.fanMode == 0 ? " selected" : "") + ">On/Off</option>";
  html += "<option value='1'" + String(settings.fanMode == 1 ? " selected" : "") + ">PWM</option>";
  html += F("</select></fieldset>");
  
  html += F("<fieldset id='pwm-params'><legend>📊 Paramètres PWM</legend>");
  html += "Kp Temp: <input type='number' name='kpt' value='" + String(settings.kpTemp, 2) + "' step='0.01' min='0.01' max='10'><br>";
  html += "Kp Hum: <input type='number' name='kph' value='" + String(settings.kpHum, 2) + "' step='0.01' min='0.01' max='10'><br>";
  html += "Poids Temp/Hum: <input type='number' name='w' value='" + String(settings.weight, 2) + "' step='0.01' min='0' max='1'><br>";
  html += F("</fieldset>");
  
  html += F("<fieldset id='onoff-params'><legend>🔘 Paramètres On/Off</legend>");
  html += "Marge Temp (°C): <input type='number' name='tmargin' value='" + String(settings.tempMargin, 1) + "' step='0.1' min='0.1' max='10'><br>";
  html += "Marge Hum (%): <input type='number' name='hmargin' value='" + String(settings.humMargin, 1) + "' step='0.1' min='0.1' max='20'><br>";
  html += F("Logique: <select name='logic'>");
  html += "<option value='0'" + String(settings.onOffLogic == 0 ? " selected" : "") + ">Humidité seule</option>";
  html += "<option value='1'" + String(settings.onOffLogic == 1 ? " selected" : "") + ">Température seule</option>";
  html += "<option value='2'" + String(settings.onOffLogic == 2 ? " selected" : "") + ">Temp ET Humidité</option>";
  html += F("</select></fieldset>");
  
  html += F("<fieldset><legend>🎛️ Contrôle Manuel</legend>");
  html += "Mode manuel: <input type='checkbox' name='manual'" + String(settings.manualMode ? " checked" : "") + "><br>";
  html += "PWM manuelle: <input type='number' name='pwm' value='" + String(settings.manualPWM) + "' step='1' min='0' max='1023'><br>";
  html += F("</fieldset>");
  
  html += F("<input type='submit' value='✅ Appliquer'> ");
  html += F("</form>");
  
  html += F("<form action='/save' style='display:inline'><input type='submit' value='💾 Sauvegarder' class='btn-save'></form> ");
  html += F("<form action='/clear' style='display:inline' onsubmit='return confirm(\"Effacer tous les paramètres ?\");'>");
  html += F("<input type='submit' value='🗑️ Reset' class='btn-reset'></form>");
  
  html += F("<script>function toggleParams(){");
  html += F("var m=document.querySelector('select[name=\"fanmode\"]').value;");
  html += F("document.getElementById('pwm-params').style.display=m=='1'?'block':'none';");
  html += F("document.getElementById('onoff-params').style.display=m=='0'?'block':'none';}");
  html += F("toggleParams();</script></div></body></html>");
  
  server.send(200, F("text/html"), html);
}

void handleStatus() {
  // Protection contre les valeurs aberrantes dans le JSON
  float safeTemp = (sensorError || isnan(temperature)) ? 0 : constrain(temperature, -99.9, 99.9);
  float safeHum = (sensorError || isnan(humidity)) ? 0 : constrain(humidity, 0.0, 99.9);
  
  String json = "{\"temp\":" + String(safeTemp, 1) + 
                ",\"hum\":" + String(safeHum, 1) + 
                ",\"fan\":" + String(constrain(fanPWMPercent, 0, 100)) + 
                ",\"uptime\":" + String(millis() / 1000UL) + 
                ",\"error\":" + ((sensorError || isnan(temperature) || isnan(humidity)) ? "true" : "false") + 
                ",\"rssi\":" + String(WiFi.RSSI()) + 
                ",\"freeMem\":" + String(ESP.getFreeHeap()) + "}";
                
  server.send(200, F("application/json"), json);
}

  
bool validateFloat(const char* arg, float& target, float min, float max) {
  if (server.hasArg(arg)) {
    String value = server.arg(arg);
    if (value.length() > 15) return false; // Limite longueur
    if (!isValidNumber(value)) return false;
    
    float val = value.toFloat();
    if (isnan(val) || isinf(val)) return false; // Vérification NaN/Inf
    if (val >= min && val <= max) {
      target = val;
      return true;
    }
    return false;
  }
  return true;
}

bool validateInt(const char* arg, int& target, int min, int max) {
  if (server.hasArg(arg)) {
    String value = server.arg(arg);
    if (value.length() > 10) return false; // Limite longueur
    if (!isValidNumber(value)) return false;
    
    long val = value.toInt();
    if (val < INT_MIN || val > INT_MAX) return false; // Vérification overflow
    if (val >= min && val <= max) {
      target = (int)val;
      return true;
    }
    return false;
  }
  return true;
}

void handleSet() {
  bool hasError = false;
  String errorMsg = "";
  
  // Sauvegarde des valeurs actuelles pour rollback
  Settings backup = settings;
  
  // Validation avec messages d'erreur détaillés
  if (!validateFloat("t", settings.tempTarget, 0, 50)) {
    hasError = true; errorMsg += "Température invalide (0-50°C). ";
  }
  if (!validateFloat("h", settings.humTarget, 0, 100)) {
    hasError = true; errorMsg += "Humidité invalide (0-100%). ";
  }
  if (!validateInt("fanmode", settings.fanMode, 0, 1)) {
    hasError = true; errorMsg += "Mode ventilateur invalide. ";
  }
  if (!validateFloat("kpt", settings.kpTemp, 0.01, 10)) {
    hasError = true; errorMsg += "Kp Temp invalide (0.01-10). ";
  }
  if (!validateFloat("kph", settings.kpHum, 0.01, 10)) {
    hasError = true; errorMsg += "Kp Hum invalide (0.01-10). ";
  }
  if (!validateFloat("w", settings.weight, 0, 1)) {
    hasError = true; errorMsg += "Poids invalide (0-1). ";
  }
  if (!validateFloat("tmargin", settings.tempMargin, 0.1, 10)) {
    hasError = true; errorMsg += "Marge Temp invalide (0.1-10°C). ";
  }
  if (!validateFloat("hmargin", settings.humMargin, 0.1, 20)) {
    hasError = true; errorMsg += "Marge Hum invalide (0.1-20%). ";
  }
  if (!validateInt("logic", settings.onOffLogic, 0, 2)) {
    hasError = true; errorMsg += "Logique On/Off invalide. ";
  }
  if (!validateInt("pwm", settings.manualPWM, 0, 1023)) {
    hasError = true; errorMsg += "PWM manuel invalide (0-1023). ";
  }
  
  settings.manualMode = server.hasArg("manual");
  
  if (hasError) {
    settings = backup; // Restauration des valeurs
    Serial.printf("❌ Paramètres rejetés: %s\n", errorMsg.c_str());
    
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    html += F("<title>Erreur - IsoBox</title></head><body>");
    html += F("<h3>❌ Paramètres invalides</h3><p>");
    html += escapeHtml(errorMsg);
    html += F("</p><a href='/'>← Retour</a></body></html>");
    
    server.send(400, F("text/html"), html);
  } else {
    Serial.println("✅ Paramètres mis à jour");
    server.sendHeader(F("Location"), F("/"));
    server.send(303);
  }
}

void handleSave() {
  saveSettings();
  server.sendHeader(F("Location"), F("/"));
  server.send(303);
}

void handleClear() {
  clearSettings();
  server.sendHeader(F("Location"), F("/"));
  server.send(303);
}

void handleNotFound() {
  server.send(404, F("text/html"), F("<h3>404 - Page non trouvée</h3><a href='/'>🏠 Accueil</a>"));
}
