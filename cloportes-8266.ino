// === Bibliothèques ===
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// === Déclarations constantes ===
#define DHTPIN D6                   // Broche de connexion du capteur DHT22
#define DHTTYPE DHT22               // Type de capteur DHT utilisé
#define OLED_RESET -1               // Aucune broche de reset utilisée pour l'écran
#define SCREEN_WIDTH 128            // Largeur de l'écran OLED
#define SCREEN_HEIGHT 32            // Hauteur de l'écran OLED
#define FAN_PIN D5                  // Broche utilisée pour activer le ventilateur (ON/OFF)

// Valeurs par défaut des consignes et marges
#define DEFAULT_TEMP_TARGET 24.0    // Consigne de température (°C)
#define DEFAULT_HUM_TARGET 55.0     // Consigne d'humidité (%)
#define DEFAULT_TEMP_MARGIN 0.5     // Marge de tolérance température (°C)
#define DEFAULT_HUM_MARGIN 2.0      // Marge de tolérance humidité (%)

// === Objets globaux ===
DHT dht(DHTPIN, DHTTYPE);                                          // Capteur DHT
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Écran OLED
ESP8266WebServer server(80);                                      // Serveur HTTP

// === Variables dynamiques ===
float tempTarget = DEFAULT_TEMP_TARGET;  // Consigne température
float humTarget  = DEFAULT_HUM_TARGET;   // Consigne humidité
float tempMargin = DEFAULT_TEMP_MARGIN;  // Marge température
float humMargin  = DEFAULT_HUM_MARGIN;   // Marge humidité
float temperature, humidity;             // Valeurs mesurées
bool fanOn = false;                      // État du ventilateur

// === Réseau Wi-Fi (mode station) ===
const char* ssid = "SSID_WIFI";  // SSID Wi-Fi
const char* password = "PASSWORD_WIFI";           // Mot de passe Wi-Fi
IPAddress localIP;                               // Adresse IP obtenue

// === SETUP ===
void setup() {
  Serial.begin(115200);            // Initialisation du port série
  dht.begin();                     // Initialisation du capteur DHT
  setupDisplay();                  // Configuration de l'écran OLED
  pinMode(FAN_PIN, OUTPUT);        // Broche du ventilateur en sortie
  digitalWrite(FAN_PIN, LOW);      // Ventilateur éteint au démarrage
  connectToWiFi();                 // Connexion au réseau Wi-Fi
  setupWebServer();                // Initialisation du serveur Web
}

// === LOOP ===
void loop() {
  server.handleClient();   // Gestion des requêtes Web
  readSensors();           // Lecture température / humidité
  computeControl();        // Contrôle ventilateur selon marges
  updateDisplay();         // Mise à jour écran OLED
  delay(1000);             // Pause d'une seconde
}

// === Écran OLED ===
void setupDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED non détecté !");
    while (1);
  }
  display.setRotation(2);     // Rotation pour orientation correcte
  display.clearDisplay();     // Nettoyage écran
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
  display.print(localIP);  // Affichage IP en bas de l'écran
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

// === Contrôle ON/OFF avec marges ===
void computeControl() {
  // Détection des dépassements de seuils
  bool tempTooHigh = temperature > (tempTarget + tempMargin);
  bool humTooHigh  = humidity  > (humTarget + humMargin);
  bool tempTooLow  = temperature < (tempTarget - tempMargin);
  bool humTooLow   = humidity  < (humTarget - humMargin);

  // Si au moins un dépassement haut : activer ventilateur
  if (tempTooHigh || humTooHigh) {
    fanOn = true;
  }
  // Si température et humidité sous les seuils bas : désactiver
  else if (tempTooLow && humTooLow) {
    fanOn = false;
  }

  // Commande du ventilateur
  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
}

// === Connexion Wi-Fi (station) ===
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

  // Tentatives avec barre de progression
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
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
    while (true);  // Blocage si échec réseau
  }
}

// === Interface Web ===
void setupWebServer() {
  server.on("/", handleRoot);   // Page principale
  server.on("/set", handleSet);  // Traitement des paramètres
  server.begin();
}

void handleRoot() {
  // Page HTML principale avec formulaire de configuration
  String html = "<html><body><h2>Controle Biosphère</h2>";
  html += "<form action='/set'>";
  html += "T consigne: <input name='t' value='" + String(tempTarget) + "'><br>";
  html += "Marge T: <input name='mt' value='" + String(tempMargin) + "'><br>";
  html += "H consigne: <input name='h' value='" + String(humTarget) + "'><br>";
  html += "Marge H: <input name='mh' value='" + String(humMargin) + "'><br>";
  html += "<input type='submit' value='Appliquer'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSet() {
  // Mise à jour des consignes et marges depuis formulaire
  if (server.hasArg("t"))  tempTarget = server.arg("t").toFloat();
  if (server.hasArg("h"))  humTarget  = server.arg("h").toFloat();
  if (server.hasArg("mt")) tempMargin = constrain(server.arg("mt").toFloat(), 0.0, 10.0);
  if (server.hasArg("mh")) humMargin  = constrain(server.arg("mh").toFloat(), 0.0, 20.0);

  // Redirection vers page principale
  server.sendHeader("Location", "/");
  server.send(303);
}
