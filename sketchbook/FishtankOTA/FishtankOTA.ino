/*
 *  Pond temperature
 *
 *  Written for an ESP8266 ESP-12E
 *  --fqbn esp8266:8266:nodemcuv2
 *
 *  update SeJ 08/22/2020 SeJ init
 *
 */


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <LittleFS.h>


/*
 * Time
 */
// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org";
//static const char ntpServerName[] = "time.nist.gov";

const int timeZone = 0;     // use UTC due to Timezone corr
//const int timeZone = -5;  // Eastern Standard Time (USA)
//const int timeZone = -4;  // Eastern Daylight Time (USA)
//const int timeZone = -8;  // Pacific Standard Time (USA)
//const int timeZone = -7;  // Pacific Daylight Time (USA)

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240};    // Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};     // Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;        // pointer to the time change rule, use to get TZ abbrev

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t getNtpTime();
const char* defaultTime = "00:00:00";
char stringTime[10];
int oldmin = 99;


/*
 * Display
 */
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # or -1 if none
Adafruit_SSD1306 display (SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


/* Passwords & Ports
 * wifi: ssid, password
 * ISY: hash, isy, isyport
 * MQTT mqtt_server, mqtt_serverport
 */
#include <../../../../../../../../../Projects/keys/sej/sej.h>


// Web Server on port 80
WiFiServer server(80);
String ServerTitle = "Jenkins FishTank";


/*
 *  MQTT
 */
const char* topic = "sej"; //  main topic
String clientId = "fishtank"; // client ID for this unit

// MQTT topics
const char* topic_status_temp = "sej/fishtank/status/temp"; // temp reading topic
const char* topic_status_templow = "sej/fishtank/status/templow"; // temp status low topic
const char* topic_status_temphigh = "sej/fishtank/status/temphigh"; // temp status hi topic
const char* topic_status_tempalarm = "sej/fishtank/status/tempalarm"; // temp status alarm topic
const char* message_status_tempalarm[] = {"OK", "LOW", "HIGH"};

const char* topic_control_templow = "sej/fishtank/control/templow"; // temp  control low topic
const char* topic_control_temphigh = "sej/fishtank/control/temphigh"; // temp  control high topic
const char* topic_control_tempalarm = "sej/fishtank/control/tempalarm"; // temp control alarm topic
const char* message_control_tempalarm[] = {"RESET"};
int templow;
int temphigh;
int tempalarm;

const char* topic_status_relay = "sej/fishtank/status/relay"; // status relay state topic
const char* message_status_relay[] = {"ON", "OFF"};
const char* topic_status_relayon = "sej/fishtank/status/relayon"; // status relay on topic
const char* topic_status_relayoff = "sej/fishtank/status/relayoff"; // status relay off topic

const char* topic_control_relay = "sej/fishtank/control/relay"; // control relay state topic
const char* message_control_relay[] = {"ON", "OFF"};
const char* topic_control_relayon = "sej/fishtank/control/relayon"; // control relay on topic
const char* topic_control_relayoff = "sej/fishtank/control/relayoff"; // control relay off topic
int relayon;
int relayoff;

const char* topic_status_hb = "sej/fishtank/status/hb"; // hb topic
const char* message_status_hb[] = {"ON", "OFF"};

const char* willTopic = topic; // will topic
byte willQoS = 0;
boolean willRetain = false;
const char* willMessage = ("lost connection " + clientId).c_str();
bool heartbeat = false; // heartbeat to mqtt

WiFiClient espClient;
PubSubClient mqttClient(espClient);
long mqttLastMsg = 0;
int mqttValue = 0;

// Data wire is plugged into pin D7 on the ESP8266 12-E - GPIO 13
#define ONE_WIRE_BUS 13

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature DS18B20(&oneWire);
float tempC;
float tempF;
float tempOld;


/*
 * timers
 */
unsigned long currentMillis = 0;
unsigned long tempMillis = 0;
const long tempInterval = 30000; // minimum 10s for DS18B20
unsigned long mqttMillis = 0;
const long mqttInterval = 30000;
unsigned long hbMillis = 0;
const long hbInterval = 60000;
unsigned long ledMillis = 0;
const long ledInterval = 3000;
bool ledState = false;
unsigned long relayMillis = 0;
const long relayInterval = 30000;
bool relayState = false;


/*
 * IO
 */
// LED_BUILTIN is D0 on ESP-12E
int relay = D6; // power relay


/*
 * Setup
 */
void setup() {
    Serial.println(F("Boot Start."));

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, !relayState); // reverse logic for relay


    Serial.begin(9600);
    delay(10);

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }

    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    display.display();
    delay(1000); // Pause for 2 seconds

    display.clearDisplay();
    display.setTextColor(WHITE, BLACK);
    display.setCursor(0,0);
    display.setTextSize(1);
                   initWifi();
                   while (WiFi.waitForConnectResult() != WL_CONNECTED) {
                       Serial.println(F("Initial Connection Failed! Rebooting..."));
                       display.println(F("Initial Connection Failed!"));
                       display.println(F("Rebooting..."));
                       display.display();
                       delay(5000);
                       ESP.restart();
                   }

                   // Port defaults to 8266
                   // ArduinoOTA.setPort(8266);

                   // Hostname defaults to esp8266-[ChipID]
                   // ArduinoOTA.setHostname("myesp8266");

                   // No authentication by default
                   // ArduinoOTA.setPassword("admin");

                   // Password can be set with it's md5 value as well
                   // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
                   // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

                   ArduinoOTA.onStart([]() {
                           String type;
                           if (ArduinoOTA.getCommand() == U_FLASH) {
                               type = "sketch";
                           } else { // U_FS
                               type = "filesystem";
                           }

                           // NOTE: if updating FS this would be the place to unmount FS using FS.end()
                           LittleFS.end();
                           Serial.println("Start updating " + type);
                       });

                   ArduinoOTA.onEnd([]() {
                           Serial.println("\nEnd");
                       });

                   ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                           Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
                       });

                   ArduinoOTA.onError([](ota_error_t error) {
                           Serial.printf("Error[%u]: ", error);
                           if (error == OTA_AUTH_ERROR) {
                               Serial.println(F("Auth Failed"));
} else if (error == OTA_BEGIN_ERROR) {
                Serial.println(F("Begin Failed"));
            } else if (error == OTA_CONNECT_ERROR) {
                Serial.println(F("Connect Failed"));
            } else if (error == OTA_RECEIVE_ERROR) {
                Serial.println(F("Receive Failed"));
            } else if (error == OTA_END_ERROR) {
                Serial.println(F("End Failed"));
            }
    });

    // OTA
    ArduinoOTA.begin();
    Serial.println(F("OTA Ready."));
    display.println(F("OTA Ready."));
    display.display();

    // time
    Udp.begin(localPort);
    setSyncProvider(getNtpTime);
    setSyncInterval(300);
    sprintf(stringTime, "%s", defaultTime);

    // MQTT
    mqttClient.setServer(mqtt_server, mqtt_serverport);
    mqttClient.setCallback(mqttCallback);
    initMQTT();

    // DS18B20
    Serial.println(F("DS18B20 Start."));
    display.println(F("DS18B20 Start."));
    display.display();
    DS18B20.begin();
    getTemperature();
    tempOld = 0;

    // LittleFS
    LittleFSConfig cfg;
    cfg.setAutoFormat(false);
    LittleFS.setConfig(cfg);
    LittleFS.begin();
    Serial.println(F("Loading config"));
    File f = LittleFS.open("/Fishtank.cnf", "r");
    if (!f) {
        //File does not exist -- first run or someone called format()
        //Will not create file; run save code to actually do so (no need here since
        //it's not changed)
        Serial.println(F("Failed to open config file"));
        templow = 75;
        temphigh = 88;
        tempalarm = 0;
        relayon = 0545;
        relayoff = 2045; // defaults
    }
    while (f.available()) {
        String key = f.readStringUntil('=');
        String value = f.readStringUntil('\r');
        f.read();
        Serial.println(key + F(" = [") + value + ']');
        Serial.println(key.length());
        if (key == F("templow")) {
            templow = value.toInt();
        }
        if (key == F("temphigh")) {
            temphigh = value.toInt();
        }
        if (key == F("tempalarm")) {
            tempalarm = value.toInt();
        }
        if (key == F("relayon")) {
            relayon = value.toInt();
        }
        if (key == F("relayoff")) {
            relayoff = value.toInt();
        }
    }
f.close();

    // clean-up
    Serial.println(F("Boot complete."));
    display.println(F("Boot Complete."));
    display.display();
    delay(1000);
    display.clearDisplay();
}


/*
 * loop
 */
void loop() {
    ArduinoOTA.handle();

    currentMillis = millis();

    // Init Wifi if dropped
    if(WiFi.status() != WL_CONNECTED) {
        display.clearDisplay();
        display.setTextColor(WHITE, BLACK);
        display.setCursor(0,0);
        display.setTextSize(1);
        Serial.println(F("Reconnecting WiFi."));
        display.println(F("Reconnecting WiFi."));
        display.display();
        initWifi();
        display.clearDisplay();
    }

    // Init MQTT if dropped
    if(mqttClient.connected()) {
        mqttClient.loop();
    } else {
        display.clearDisplay();
        display.setTextColor(WHITE, BLACK);
        display.setCursor(0,0);
        display.setTextSize(1);
        Serial.println(F("Reconnecting MQTT."));
        display.println(F("Reconnecting MQTT."));
        display.display();
        initMQTT();
        display.clearDisplay();
    }

    // local led hb
    if(currentMillis - ledMillis > ledInterval) {
        ledMillis = currentMillis;
        ledState = not(ledState);
        digitalWrite(LED_BUILTIN, ledState);
    }

    // MQTT Heartbeat
    if(currentMillis - hbMillis > hbInterval) {
        hbMillis = currentMillis;
        heartbeat = not(heartbeat);
        if(mqttClient.connected()) {
            mqttClient.publish(topic_status_hb, message_status_hb[heartbeat] , true);
        }
    }

    // time
    if(timeStatus() == timeSet) {
        time_t local = myTZ.toLocal(now(), &tcr);
        sprintf(stringTime, "%02d:%02d", hour(local), minute(local));
        if(oldmin != minute()){
            oldmin = minute();
            display.setTextSize(2);
            display.setCursor(0,0);
            display.print(stringTime);
            display.display();
        }
    } else {
        sprintf(stringTime, "%s", defaultTime);
    }

    // relay control
    if(currentMillis - relayMillis > relayInterval) {
        relayMillis = currentMillis;
        // relayState = not(relayState);
        updateRelay();
    }

    // Temperature retrieve & publish
    if(currentMillis - tempMillis > tempInterval) {
        tempMillis = currentMillis;
        getTemperature();
        if (tempOld != tempF && tempF > 0){
            Serial.print(F("Temp in Celsius: "));
            Serial.print(tempC,2);
            Serial.print(F("   Temp in Fahrenheit: "));
            Serial.println(tempF,2);

            display.setCursor(0,16);
            display.setTextSize(2);
            display.print(F("int:"));
            display.print(tempF,2);
            display.print(F("F"));
            display.display();

            if(tempF > temphigh){
                tempalarm = 2; // set high alarm
            } else if(tempF < templow){
                tempalarm = 1; // set low alarm
            } else {
                tempalarm = 0; // reset alarm TODO only do this on control reset
            }

            if(mqttClient.connected()) {
                const size_t capacity = JSON_OBJECT_SIZE(2);
                StaticJsonDocument<capacity> doc;
                JsonObject obj = doc.createNestedObject("DS18B20");
                obj["Temperature"] = tempF;

                char buffer[256];
                serializeJson(doc, buffer);
                mqttClient.publish(topic_status_temp, buffer, true);
                sprintf(buffer, "%3d", templow);
                mqttClient.publish(topic_status_templow, buffer, true);
                sprintf(buffer, "%3d", temphigh);
                mqttClient.publish(topic_status_temphigh, buffer, true);
                mqttClient.publish(topic_status_tempalarm, message_status_tempalarm[tempalarm], true);
            }
        }
    }

    // Web Client
    // Listening for new clients
    WiFiClient client = server.available();
    if (client) {
        Serial.println(F("New client"));
        // bolean to locate when the http request ends
        boolean blank_line = true;
        while (client.connected()) {
            if (client.available()) {
                char c = client.read();
                if (c == '\n' && blank_line) {
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: text/html"));
                    client.println(F("Connection: close"));
                    client.println();
                    // web page that displays temperature
                    client.println(F("<!DOCTYPE HTML><html><head></head><body><h1>"));
                    client.println(ServerTitle);
                    client.println(F("</h1><h3>Temperature in Celsius: "));
                    client.println(tempC,2);
                    client.println(F("*C</h3><h3>Temperature in Fahrenheit: "));
                    client.println(tempF,2);
                    client.println(F("*F</h3><h3>Relay State: "));
                    client.println(relayState, BIN);
                    client.println(F("</h3><h3>"));
                    client.println(stringTime);
                    client.println(F("</h3></body></html>"));
                    break;
                }
                if (c == '\n') {
                    // when starts reading a new line
                    blank_line = true;
                }
                else if (c != '\r') {
                    // when finds a character on the current line
                    blank_line = false;
                }
            }
        }
        // closing the client connection
        delay(1);
        client.stop();
        Serial.println(F("WebClient disconnected."));
    }

}


// Subroutines

/*
 * Establish Wi-Fi connection & start web server
 */
void initWifi() {
	Serial.println("");
	Serial.println(F("Connecting to: "));
    Serial.print(ssid);
    display.setTextSize(1);
    display.println(F("Connecting to: "));
    display.println(ssid);
    display.display();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int timeout = 25 * 4; // 25 seconds
    while(WiFi.status() != WL_CONNECTED  && (timeout-- > 0)) {
        delay(250);
        Serial.print(".");
    }
    Serial.println("");

    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed.");
        display.println("Wifi failed.");
        display.display();
    }
    else {
        Serial.println(F("Connected."));
        Serial.print(F("IP: "));
        Serial.println(WiFi.localIP());
        display.println(F("Connected."));
        display.print(F("IP: "));
        display.println(WiFi.localIP());
        display.display();

        // Starting the web server
        server.begin();
    }
}


/*
 * MQTT client init connection
 */
void initMQTT() {
    while (!mqttClient.connected() && ((currentMillis == 0) || (currentMillis - mqttMillis > mqttInterval))) {
        Serial.print(F("Attempting MQTT connection..."));

        // Attempt to connect
        if (mqttClient.connect(clientId.c_str(), clientId.c_str(), password,
                               willTopic, willQoS, willRetain, willMessage)) {
            Serial.println(F("connected"));
            display.setTextSize(1);
            display.println(F("MQTT connected."));
            display.display();
            // Once connected, publish an announcement...
            mqttClient.publish(topic, ("connected " + clientId).c_str() , true );
            mqttClient.subscribe(topic_control_relay);
            mqttClient.subscribe(topic_control_relayon);
            mqttClient.subscribe(topic_control_relayoff);
            mqttClient.subscribe(topic_control_templow);
            mqttClient.subscribe(topic_control_temphigh);
            mqttClient.subscribe(topic_control_tempalarm);
        } else {
            Serial.print(F("failed, rc="));
            Serial.print(mqttClient.state());
            Serial.print(F(" wifi="));
            Serial.println(WiFi.status());
            mqttMillis = currentMillis;
        }
    }
}


/*
 * MQTT Callback message
 */
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    char mypayload[length+1];
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
        mypayload[i] = (char)payload[i];
    }
    mypayload[length] = '\0';
    Serial.println();

    // Switch on the RELAY if an ON received
    if (strcmp((char *)mypayload, (char *)message_control_relay[0]) == 0) {
        relayState = true;
        updateRelay();
    } else if (strcmp((char *)mypayload, (char *)message_control_relay[1]) == 0) {
        relayState = false;
        updateRelay();
    }
}


/*
 * update Relay output
 */
void updateRelay() {
    digitalWrite(relay, !relayState); // reverse logic for relay
    Serial.print("Relay: ");
    Serial.print(message_status_relay[!relayState]);
    display.setTextSize(2);
    display.setCursor(0,48);
    display.print("relay:");
    display.print(message_status_relay[!relayState]);
    display.print(" ");
    display.display();
    if(mqttClient.connected()) {
        mqttClient.publish(topic_status_relay, message_status_relay[!relayState], true);
    }
}


/*
 * GetTemperature from DS18B20
 */
void getTemperature() {
	do {
		DS18B20.requestTemperatures();
		tempC = DS18B20.getTempCByIndex(0);
		tempF = DS18B20.getTempFByIndex(0);
		delay(100);
	} while (tempC == 85.0 || tempC == (-127.0));
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void saveConfig() {
    Serial.println(F("Saving config."));
    File f = LittleFS.open("/Fishtank.cnf", "w+");
    f.print(F("templow="));
    f.println(templow);
    f.print(F("temphigh="));
    f.println(temphigh);
    f.print(F("tempalarm="));
    f.println(tempalarm);
    f.print(F("relayon="));
    f.println(relayon);
    f.print(F("relayoff="));
    f.println(relayoff);
    f.flush();
    f.close();
    Serial.println(F("Saved values."));
}
