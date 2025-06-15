// === Bibliothèques ===
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// === Déclarations constantes ===
#define DHTPIN D6
#define DHTTYPE DHT22
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define FAN_PIN D5

#define DEFAULT_TEMP_TARGET 24.0
#define DEFAULT_HUM_TARGET 55.0
#define DEFAULT_TEMP_MARGIN 0.5
#define DEFAULT_HUM_MARGIN 2.0
#define DEFAULT_WEIGHT 0.5    // Poids par défaut entre 0.0 (humidité) et 1.0 (température)

// === Objets globaux ===
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);

// === Variables dynamiques ===
float tempTarget = DEFAULT_TEMP_TARGET;
float humTarget  = DEFAULT_HUM_TARGET;
float tempMargin = DEFAULT_TEMP_MARGIN;
float humMargin  = DEFAULT_HUM_MARGIN;
float weight     = DEFAULT_WEIGHT;  // Poids entre température (1) et humidité (0)

float temperature, humidity;
bool fanOn = false;

// === Réseau Wi-Fi ===
const char* ssid = "SSID_WIFI";
const char* password = "PASSWORD_WIFI";
IPAddress localIP;

// === SETUP ===
void setup() {
  Serial.begin(115200);
  dht.begin();
  setupDisplay();
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  connectToWiFi();
  setupWebServer();
}

// === LOOP ===
void loop() {
  server.handleClient();
  readSensors();
  computeWeightedControl();
  updateDisplay();
  delay(1000);
}

// === OLED ===
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
  display.printf("FAN: %s\n", fanOn ? "ON" : "OFF");
  display.setCursor(0, 24);
  display.print(localIP);
  display.display();
}

// === Capteurs ===
void readSensors() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("⚠️ Erreur capteur DHT22");
  }
}

// === Contrôle avec pondération ===
void computeWeightedControl() {
  float tempError = temperature - tempTarget;
  float humError  = humidity - humTarget;

  // Normalisation des erreurs (divisé par marges)
  float normTemp = tempError / tempMargin;
  float normHum  = humError  / humMargin;

  // Score combiné pondéré
  float score = weight * normTemp + (1.0 - weight) * normHum;

  // Activation si score > 1, désactivation si score < -1
  if (score > 1.0) {
    fanOn = true;
  } else if (score < -1.0) {
    fanOn = false;
  }

  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
}

// === Connexion Wi-Fi ===
void connectToWiFi() {
  Serial.println("🔧 Connexion à la box Wi-Fi...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Connexion Wi-Fi...");
  display.display();

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 45) {
    delay(500);
    Serial.print(".");
    display.fillRect(0, 16, attempts * 4, 8, SSD1306_WHITE);
    display.display();
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP();
    Serial.println("\n✅ Connecté au Wi-Fi !");
    Serial.print("🌐 IP : ");
    Serial.println(localIP);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Wi-Fi Connecté !");
    display.print("IP: ");
    display.println(localIP);
    display.display();
  } else {
    Serial.println("\n❌ Connexion échouée !");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("❌ Wi-Fi échoué");
    display.display();
    while (true);
  }
}

// === Serveur Web ===
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
}

void handleRoot() {
  String html = "<html><body><h2>Contrôle Biosphère</h2>";
  html += "<form action='/set'>";
  html += "T consigne: <input name='t' value='" + String(tempTarget) + "'><br>";
  html += "Marge T: <input name='mt' value='" + String(tempMargin) + "'><br>";
  html += "H consigne: <input name='h' value='" + String(humTarget) + "'><br>";
  html += "Marge H: <input name='mh' value='" + String(humMargin) + "'><br>";
  html += "Poids T/H [0-1]: <input name='w' value='" + String(weight) + "'><br>";
  html += "<input type='submit' value='Appliquer'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSet() {
  if (server.hasArg("t"))  tempTarget = server.arg("t").toFloat();
  if (server.hasArg("h"))  humTarget  = server.arg("h").toFloat();
  if (server.hasArg("mt")) tempMargin = constrain(server.arg("mt").toFloat(), 0.01, 10.0);
  if (server.hasArg("mh")) humMargin  = constrain(server.arg("mh").toFloat(), 0.01, 20.0);
  if (server.hasArg("w"))  weight     = constrain(server.arg("w").toFloat(), 0.0, 1.0);

  server.sendHeader("Location", "/");
  server.send(303);
}
