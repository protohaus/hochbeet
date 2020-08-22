/*Dieses Programm für ESP32 DevKits verbindet sich mit dem Protohaus-Wifi
  und dem dortigen Node-RED. Letzteres kann die Bewässerungspumpe des 
  Hochbeets schalten und erhält periodisch den Wasserstand in den Tanks. */

#include <Arduino.h>
#include <WiFi.h>         //für Wlan-Anbindung
#include <PubSubClient.h> //für MQTT-Anbindung
#include <WifiMulti.h>    //damit mehrere Wifi gespeichert werden können

//  === Variablendeklaration  ===

//WLAN Zugangsdaten
const char *ssid = "PROTOHAUS";            //Netzwerkname
const char *password = "PH-Wlan-2016#";    //Netzwerkpasswort
const char *protohaus_mqtt_server = "192.168.0.170"; //IP des IOT-Servers im Netzwerk
const char *greenhouse_mqtt_server = "172.31.0.1"; 

//ESP als WLAN Client von MQTT-BROKER
WiFiClient espHochbeet; //Name des IOT-Device im Netzwerk
PubSubClient client(espHochbeet);
WiFiMulti wifiMulti;

//IO ESP
const int lamp = 2;   //OnBoard Lampe für Funktionstest
const int relay = 12; //Relais für Pumpschaltung

//Arrays für Wasserstandsmessung
const int wasserTanks = 1;           //Anzahl Wassertanks
int waterVolt[wasserTanks] = {27};   //Wassermessung aktivieren
int waterSignal[wasserTanks] = {34}; //Wassersensoren

//  === Funktionsdeklaration  ===

// ==Wasserstand==

String waterLevel(int stand) //Tankfüllstand
{
  String level; //Tankfüllung in Prozent

  //Temporär müssen Füllstände auch hier übertragen werden, eigentlich siehe waterPrint"Übergabe"

  //Bereiche für Füllstandseinteilung
  if (stand > 0 && stand <= 400) //Keine Messeinrichtung im Wasser
  {
    client.publish("HB::H20Lvl", "< 10");
    level = "< 10";
  }
  else if (stand > 400 && stand <= 500) //Pin 1 und 2 im Wasser
  {
    client.publish("HB::H20Lvl", "10 - 20");
    level = "10 - 20";
  }
  else if (stand > 500 && stand <= 650) //Pin 3 auch
  {
    client.publish("HB::H20Lvl", "20 - 40");
    level = "20 - 40";
  }
  else if (stand > 650 && stand <= 900) //Pin 4 auch
  {
    client.publish("HB::H20Lvl", "40 - 60");
    level = "40 - 60";
  }
  else if (stand > 900 && stand <= 2000) //Pin 5 auch
  {
    client.publish("HB::H20Lvl", "60 - 80");
    level = "60 - 80";
  }
  else if (stand > 2000 && stand <= 3600) // Pin 6 auch
  {
    client.publish("HB::H20Lvl", "> 80");
    level = "> 80";
  }
  else
  {
    client.publish("HB::H20Lvl", "FEHLER");
    level = "FEHLER";
  }

  return level; //Ausgabe der Tankfüllung
}

//gibt Messwerte der Wasserstandsmessung im seriellen Monitor aus
void waterPrint(int printNum, int printSmooth, String printTemp)
{ //Tanknummer, Rohmesswert, Glattmesswert, Füllstandslevel

  //Messwerte im Seriellen Monitor ausgeben
  Serial.print("Wasserstandswerte ( Glatt / Füllstand ) für Tank an Pin ");
  Serial.print(printNum); //Tanknummer
  Serial.print(": ");
  Serial.print(printSmooth); //Glattmesswert
  Serial.print(" / ");
  Serial.print(printTemp); //Füllstandlevel
  Serial.println(" %");

  //Übergabe
  //client.publish("HB::H20Lvl", printTemp);
}

//Messfunktion für Wasserstandsmessung
void waterWrite(int input) //Sensorpin vom Tank
{
  int raw = 0;
  int messungen = 10;
  int smooth = 0;

  //Messwerte auslesen und Rauschen entfernen
  for (int i = 0; i < messungen; i++)
  {
    raw = analogRead(input); //Wert einlesen
    Serial.print(raw);
    Serial.print(" ");
    delay(100);
    smooth += raw;
  }

  smooth /= messungen;

  //geglättete Messwerte in Bereiche einteilen
  String waterTemp = waterLevel(smooth);

  //Messwerte des Tanks an Seriellen Monitor senden
  waterPrint(input, smooth, waterTemp);
}

//Wasserstandsmessung mit Arrays
void waterMe()
{
  //Messung für je einen Tank nacheinander
  for (int i = 0; i < wasserTanks; i++)
  {
    //Starte Messung durch Anlegen von Strom
    digitalWrite(waterVolt[i], HIGH);
    delay(500);

    //Messfunktion für jeweiligen Tank
    waterWrite(waterSignal[i]);

    //Beende Messung durch Abschalten von Strom
    delay(500);
    digitalWrite(waterVolt[i], LOW);
  }
}

// ==MQTT-Verbindung==

//Verbindungsherstellung mit vordefiniertem WLAN-Netzwerk
void setup_wifi()
{
  delay(10);

  Serial.println();
  Serial.print("Connecting to: ");
  Serial.println(ssid);       //zeigt Netzwerkname, mit dem sich verbunden wird
  WiFi.begin(ssid, password); //wählt sich in WLAN mit gespeicherten Zugangsdaten ein

  // wenn keine sofortige Verbindung zeige "..." bis verbunden
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(". "); //"Punkt Punkt Punkt"
  }

  //wenn Verbindung erfolgreich:
  Serial.println("");
  Serial.print("Wifi connected - ESP IP adress: ");
  Serial.println(WiFi.localIP()); //zeige IP-Adresse dieses Device im Netzwerk im Serial an
}

//IOT-Communication
void callback(String topic, byte *message, unsigned int length)
{
  // topic: I/O-Bezeichnung
  // message: I/O-Signal
  // length: Länge des signals

  //Ausgabe im Seriellen Monitor zur Kontrolle
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");

  //speichere [Message] für Programmablauf String messageTemp (Temporäres Message)
  String messageTemp;
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }

  Serial.println(); //Linebreak

  //Output-Signale über MQTT
  //An/Aus und Ping an Lampe auf ESP
  if (topic == "HB::lamp") //falls topic = on board lampe -> schalte Lampe entsprechend message
  {
    if (messageTemp == "ping") //Ping für Funktionstest
    {
      Serial.print("Pinging on board lamp: ");

      for (int i = 0; i < 5; i++) //Lampe blinkt 5 mal
      {
        digitalWrite(lamp, HIGH);
        delay(250);

        Serial.print(". ");

        digitalWrite(lamp, LOW);
        delay(400);
      }

      Serial.print("Done.");
    }
  }

  //Pumpe für Bewässerung über Relais
  if (topic == "HB::pump")
  {
    if (messageTemp == "1")
    {
      digitalWrite(relay, HIGH);
      Serial.println("Pumpe an.");
    }

    else if (messageTemp == "0")
    {
      digitalWrite(relay, LOW);
      Serial.println("Pumpe aus.");
    }
  }

  if (topic == "HB::H20Lvl")
  {
    if (messageTemp == "check")
    {
      Serial.println("Wasserstandsmessung gestartet...");
      waterMe();
    }
  }

  //--->>> hier neue Output Signale (=topics) einfügen über ifs

  Serial.println(); //Linebreak
}

//Neuverbindung nach Verbindungsabbruch
void reconnect()
{ //Loop solange Verbindung nicht vorhanden
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection ... ");

    //... Verbindung ok und zeige device-name an
    if (client.connect("ESP Hochbeet"))
    {
      Serial.println("connected.");

      //WICHTIG: WENN NEUE FUNKTION -> SUBSRIPTION ZUM TOPIC HINZUFÜGEN!
      client.subscribe("HB::lamp");   //Test Hardwarelampe
      client.subscribe("HB::pump");   //Relais für Pumpe
      client.subscribe("HB::H20Lvl"); //Wasserstandsabfrage
    }

    //... Verbindung nicht ok
    else
    {
      Serial.print("failed, rc= ");
      Serial.print(client.state()); //???

      Serial.println(" try again in 5 sec");
      delay(5000);
    }
  }
}

//  === Hauptprogrammcode  ===

void setup()
{
  //Pin für Kontrolllampe
  pinMode(lamp, OUTPUT); //Modus On Board LED

  //Pin für Pumpe
  pinMode(relay, OUTPUT);
  digitalWrite(relay, LOW);

  //Pins Wasserstand Array
  for (int i = 0; i < wasserTanks; i++)
  {
    pinMode(waterVolt[i], OUTPUT);
    pinMode(waterSignal[i], INPUT);
  }

  Serial.begin(115200); //Aktiviere seriellen Monitor

  //Wifi starten

  wifiMulti.addAP("PROTOHAUS", "PH-Wlan-2016#");
  wifiMulti.addAP("SDGintern", "8037473183859244");

  Serial.println("Connecting Wifi...");
  if (wifiMulti.run() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  //setup_wifi();
  String ipAddress = WiFi.localIP().toString();
  if(ipAddress.startsWith("172")) {
    client.setServer(greenhouse_mqtt_server, 1883);
    Serial.println("Greenhouse MQTT");
  } else if(ipAddress.startsWith("192")) {
    client.setServer(protohaus_mqtt_server, 1883);
    Serial.println("Protohaus MQTT");
  } else {
    int timeStamp = millis();

    while( millis() - timeStamp <= 50000) {
      digitalWrite(lamp, HIGH);
      delay(100);
      digitalWrite(lamp, LOW);
      delay(100);
    }
  }
  client.setCallback(callback);
}

void loop()
{
  if (!client.connected())
    reconnect();

  if (!client.loop())
    client.connect("ESP Hochbeet");

  if (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected!");
    delay(1000);
  }

  //Für Wasserstand debug
  //waterMe();
  //delay(5000);
}