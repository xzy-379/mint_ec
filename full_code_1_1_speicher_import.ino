#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>


const char* ssid = "MQTT-MINT";
const char* password = "12mint34!";
const char* mqtt_server = "mqtt.sva.de";
#define mqtt_port 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define BUFFER_SIZE JSON_OBJECT_SIZE(300)

#define LED_BUILTIN 2

WiFiClient wifiClient;
PubSubClient client(wifiClient);

int server_time = 0;

int stromspeicher = 2000;
int max_speicher = 4000;

int importierter_strom = 0;

#define DIGITAL_PIN 23  //GPI23
boolean ldr = false;
String light;

void buildJson(char* buffer, size_t bufSize, int value) {
  JsonDocument doc;

  // Einfache Werte
  doc["value"] = value;
  doc["unit"] = "kW";

  // Serialisieren in Buffer
  serializeJson(doc, buffer, bufSize);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
void sendEnergy(char* gebäude, int value) {

  char buffer[256];
  buildJson(buffer, sizeof(buffer), value);
  char* path = "/energie/zufuhr";
  char buf[100];
  strcpy(buf, gebäude);
  strcat(buf, path);
  client.publish(buf, buffer);
}

//Feuerwache Hochhaus Tiefgarage
void distributeEnergy(int value, bool day) {
  int used_energy = 0;
  char buffer[256];
  buildJson(buffer, sizeof(buffer), value);
  client.publish("solarpark/energie/gesamt", buffer);

  buffer[256];
  buildJson(buffer, sizeof(buffer), stromspeicher);
  client.publish("solarpark/energie/stromspeicher", buffer);

  buffer[256];
  buildJson(buffer, sizeof(buffer), importierter_strom);
  client.publish("solarpark/energie/importiert", buffer);


  used_energy += 155;
  sendEnergy("feuerwache", 50);
  sendEnergy("tiefgarage", 50);
  sendEnergy("katastrophenschutz", 50);
  sendEnergy("wetterstation", 5);
  if (day) {
    used_energy += 200;
    sendEnergy("solarpark", 200);
    used_energy += 270;
    int extra_energy;
    if (value > 1165) {
      extra_energy = 540;

    } else {
      extra_energy = value - used_energy;  // maximal 575
    }
    sendEnergy("einkaufszentrum", 100 + extra_energy * 13 / 27);
    sendEnergy("hochhaus", 20 + extra_energy * 1 / 3);
    sendEnergy("flughafen", 150 + extra_energy * 5 / 27);
    used_energy += extra_energy;

  } else {
    used_energy += 140;
    sendEnergy("einkaufszentrum", 20);
    sendEnergy("hochhaus", 20);
    sendEnergy("solarpark", 100);
    int extra_energy;
    used_energy += 100;
    if (value > 545) {
      extra_energy = 150;
    } else {
      extra_energy = value - used_energy;
    }
    sendEnergy("flughafen", 100 + extra_energy);
    used_energy += extra_energy;
  }


  buildJson(buffer, sizeof(buffer), used_energy);
  client.publish("solarpark/energie/verbrauch", buffer);
}

void callback(char* topic, byte* payload, unsigned int length) {

  // Serial.println("Timer Lesen");
  // Serial.println(topic);

  StaticJsonDocument<BUFFER_SIZE> jsonDoc;
  deserializeJson(jsonDoc, payload, length);

  if (!jsonDoc["hour"].isNull()) {
    int number = jsonDoc["hour"].as<int>();
    server_time = number;
    Serial.print("New Time: ");
    Serial.println(number);
  }
  ldr = digitalRead(DIGITAL_PIN);

  // DAY/NIGHT
  bool day = server_time <= 20 && server_time >= 8;

  // main
 
  digitalWrite(LED_BUILTIN, day ? HIGH : LOW);

  String light = "No";
  int verfügbare_energie = day && !ldr ?  random(1000, 1800) : 0;

  // Ohne Sonne gespeicherten Strom verwenden
  bool genug_speicher = (day && stromspeicher > 625) || (!day && stromspeicher > 395);
  if (genug_speicher && verfügbare_energie == 0) {
    verfügbare_energie += day ? 625 : 395;
    stromspeicher -= day ? 625 : 395;
  }
  // Tagsüber Strom speichern
  if (day && verfügbare_energie > 1165) {
    stromspeicher += verfügbare_energie - 1165;
    verfügbare_energie = 1165;
  }
  // Beim vollen Speicher überschüssige Energie verwenden
  if (stromspeicher > max_speicher) {
    verfügbare_energie += stromspeicher - max_speicher;
    stromspeicher = max_speicher;
    if (verfügbare_energie>1165){
      importierter_strom -=verfügbare_energie-1165;
      verfügbare_energie=1165;
    }
  }

  // Wenn der Speicher nicht reicht Strom importieren
  if (verfügbare_energie == 0 && !genug_speicher) {
    verfügbare_energie += day ? 625 : 395;
    importierter_strom += day ? 625 : 395;
  }

  
  // Serial.print("Light detected: ");
  // Serial.println(light);
  int energy = verfügbare_energie;
  Serial.print("Energie: ");
  Serial.println(energy);
  Serial.print("Speicher: ");
  Serial.println(stromspeicher);
  distributeEnergy(energy, day);
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected");
      //Once connected, publish an announcement...
      //  client.publish("/test", "hello world (SOLARPARK)");
      // ... and resubscribe
      client.subscribe("timer");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {

  Serial.begin(115200);    //Serial Monitor für Wlan/MQTT
  Serial.setTimeout(500);  // Set time out
  setup_wifi();
  client.setBufferSize(BUFFER_SIZE);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  reconnect();

  pinMode(DIGITAL_PIN, INPUT);   // Sensor
  pinMode(LED_BUILTIN, OUTPUT);  // LED
}

void loop() {
  client.loop();
  delay(200);
}
