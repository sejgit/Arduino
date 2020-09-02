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


/* Passwords & Ports
 * wifi: ssid, password
 * ISY: hash, isy, isyport
 * MQTT mqtt_server, mqtt_serverport
 */
#include <../../../../../../../../../Projects/keys/sej/sej.h>


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
time_t local;


/*
 * Display
 */
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # or -1 if none
Adafruit_SSD1306 display (SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// Web Server on port 80
WiFiServer server(80);
String ServerTitle = "Jenkins FishTank";


/*
 *  MQTT
 */
const char* topic = "sej"; //  main topic
String clientId = "fishtank"; // client ID for this unit
char buffer[256];

// MQTT topics
const char* topic_status_temp = "sej/fishtank/status/temp"; // temp reading topic
const char* topic_status_templow = "sej/fishtank/status/templow"; // temp status low topic
const char* topic_status_temphigh = "sej/fishtank/status/temphigh"; // temp status hi topic
const char* topic_status_tempalarm = "sej/fishtank/status/tempalarm"; // temp status alarm topic
const char* message_status_tempalarm[] = {"OK", "LO", "HI"};

const char* topic_control_templow = "sej/fishtank/control/templow"; // temp  control low topic
const char* topic_control_temphigh = "sej/fishtank/control/temphigh"; // temp  control high topic
const char* topic_control_tempalarm = "sej/fishtank/control/tempalarm"; // temp control alarm topic
const char* message_control_tempalarm[] = {"--", "RESET"};
float tempLow;
float tempHigh;
int tempAlarm;

const char* topic_status_relay = "sej/fishtank/status/relay"; // status relay state topic
const char* message_status_relay[] = {"ON", "OFF"};
const char* topic_status_relayon = "sej/fishtank/status/relayon"; // status relay on topic
const char* topic_status_relayoff = "sej/fishtank/status/relayoff"; // status relay off topic

const char* topic_control_relay = "sej/fishtank/control/relay"; // control relay state topic
const char* message_control_relay[] = {"ON", "OFF"};
const char* topic_control_relayon = "sej/fishtank/control/relayon"; // control relay on topic
const char* topic_control_relayoff = "sej/fishtank/control/relayoff"; // control relay off topic
int relayON;
int relayOFF;

const char* topic_status_hb = "sej/fishtank/status/hb"; // hb topic
const char* message_status_hb[] = {"ON", "OFF"};

const char* message_status_server[] = {"NOK", "OK"};

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

// cfg updates
boolean cfgChangeFlag = false;


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
int resetButton = D5 ;


/*
 * Declare Subroutines
 */

/*
 * Establish Wi-Fi connection & start web server
 */
boolean initWifi(int tries = 2, int waitTime = 2) {
    int status = WL_IDLE_STATUS;
    WiFi.mode(WIFI_STA);

    while(status != WL_CONNECTED && (tries-- > 0)) {
        status = WiFi.begin(ssid, password);
        int timeout = waitTime;

        while (WiFi.status() != WL_CONNECTED && (timeout-- > 0)) {
            delay(1000);
        }
        if (WiFi.status() == WL_CONNECTED) {
            break;
        }
    }

    if(WiFi.status() != WL_CONNECTED) {
        return false;
    }
    else {
        // Starting the web server
        server.begin();
        return true;
    }
}


/*
 * MQTT client init connection
 */
boolean initMQTT() {
    Serial.print(F("Attempting MQTT connection..."));

    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), clientId.c_str(), password,
                           willTopic, willQoS, willRetain, willMessage)) {
        Serial.println(F("connected"));
        // Once connected, publish an announcement...
        mqttClient.publish(topic, ("connected " + clientId).c_str() , true );
        mqttClient.subscribe(topic_control_relay);
        mqttClient.subscribe(topic_control_relayon);
        mqttClient.subscribe(topic_control_relayoff);
        mqttClient.subscribe(topic_control_templow);
        mqttClient.subscribe(topic_control_temphigh);
        mqttClient.subscribe(topic_control_tempalarm);
        return true;
    } else {
        return false;
    }
}


/*
 * refresh config parameters to MQTT
 */
boolean mqttRefreshConfig() {
    if(mqttClient.connected()) {
        sprintf(buffer, "%3d.%02d", (int)tempLow, (int)(tempLow*100)%100);
        mqttClient.publish(topic_status_templow, buffer, true);
        sprintf(buffer, "%3d.%02d", (int)tempHigh, (int)(tempHigh*100)%100);
        mqttClient.publish(topic_status_temphigh, buffer, true);
        sprintf(buffer, "%04d", relayON);
        mqttClient.publish(topic_status_relayon, buffer, true);
        sprintf(buffer, "%04d", relayOFF);
        mqttClient.publish(topic_status_relayoff, buffer, true);
        if(tempF > tempHigh){
            tempAlarm = 2; // set high alarm
        } else if(tempF < tempLow){
            tempAlarm = 1; // set low alarm
        }
        mqttClient.publish(topic_status_tempalarm, message_status_tempalarm[tempAlarm], true);
        return true;
    } else {
        return false;
    }
}


/*
 * update Relay output
 */
void updateRelay(boolean updateDisplay) {
    digitalWrite(relay, !relayState); // reverse logic for relay
    Serial.print(F("Relay: "));
    Serial.print(message_status_relay[!relayState]);
    if(updateDisplay){
        display.setTextSize(1);
        display.setCursor(96,0);
        display.print(F("R:"));
        display.print(message_status_relay[!relayState]);
        display.print(" ");
        display.display();
    }
    if(mqttClient.connected()) {
        mqttClient.publish(topic_status_relay, message_status_relay[!relayState], true);
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

    // Switch the RELAY if an ON or OFF is received
    if(strcmp(topic, topic_control_relay)==0){
        if (strcmp((char *)mypayload, (char *)message_control_relay[0]) == 0) {
            relayState = true;
            updateRelay(true);
        } else if (strcmp((char *)mypayload, (char *)message_control_relay[1]) == 0) {
            relayState = false;
            updateRelay(true);
        }
    }

    // RELAY ON time received
    if(strcmp(topic, topic_control_relayon)==0){
        int hrtemp = atoi(mypayload) / 100;
        int mintemp = atoi(mypayload) - (hrtemp * 100);
        // convert to int then check if between 0000 & 2359 before setting relayON
        if (hrtemp >= 0 && hrtemp <= 23) {
            if (mintemp >= 0 && mintemp <= 59) {
                cfgChangeFlag = true;
                relayON = (hrtemp * 100) + mintemp;
                sprintf(buffer, "%04d", relayON);
                if(mqttClient.connected()) {
                    mqttClient.publish(topic_status_relayon, buffer, true);
                }
            }
        }
    }

    // RELAY OFF time received
    if(strcmp(topic, topic_control_relayoff)==0){
        int hrtemp = atoi(mypayload) / 100;
        int mintemp = atoi(mypayload) - (hrtemp * 100);
        // convert to int then check if between 0000 & 2359 before setting relayOFF
        if (hrtemp >= 0 && hrtemp <= 23) {
            if (mintemp >= 0 && mintemp <= 59) {
                cfgChangeFlag = true;
                relayOFF = (hrtemp * 100) + mintemp;
                sprintf(buffer, "%04d", relayOFF);
                if(mqttClient.connected()) {
                    mqttClient.publish(topic_status_relayoff, buffer, true);
                }
            }
        }
    }

    // temp low received
    if(strcmp(topic, topic_control_templow)==0){
        float temptemp = atof(mypayload);
        // convert to float then check if between 40 & 150 before setting tempLow
        if (temptemp >= 40.00 && temptemp <= 150.00 && temptemp < tempHigh) {
            cfgChangeFlag = true;
            tempLow = temptemp;
            sprintf(buffer, "%3d.%02d", (int)tempLow, (int)(tempLow*100)%100);
            if(mqttClient.connected()) {
                mqttClient.publish(topic_status_templow, buffer, true);
            }
        }
    }

    // temp high received
    if(strcmp(topic, topic_control_temphigh)==0){
        float temptemp = atof(mypayload);
        // convert to float then check if between 40 & 150 before setting tempHigh
        if (temptemp >= 40.00 && temptemp <= 150.00 && temptemp > tempLow) {
            cfgChangeFlag = true;
            tempHigh = temptemp;
            sprintf(buffer, "%3d.%02d", (int)tempHigh, (int)(tempHigh*100)%100);
            if(mqttClient.connected()) {
                mqttClient.publish(topic_status_temphigh, buffer, true);
            }
        }
    }

    // temp alarm potential reset received
    if(strcmp(topic, topic_control_tempalarm)==0){
        // check if reset message
        if (strcmp(mypayload, message_control_tempalarm[1]) == 0) {
            cfgChangeFlag = true;
            tempAlarm = 0;
            tempMillis = 0;
            if(mqttClient.connected()) {
                mqttClient.publish(topic_status_tempalarm, message_status_tempalarm[0], true);
                mqttClient.publish(topic_control_tempalarm, message_control_tempalarm[0], true);
            }
        }
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


/*
 * NTP code
 */
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


/*
 * saveConfig file
 */
void saveConfig() {
    Serial.println(F("Saving config."));
    File f = LittleFS.open("/Fishtank.cnf", "w+");
    f.print(F("tempLow="));
    f.println(tempLow);
    f.print(F("tempHigh="));
    f.println(tempHigh);
    f.print(F("tempAlarm="));
    f.println(tempAlarm);
    f.print(F("relayON="));
    f.println(relayON);
    f.print(F("relayOFF="));
    f.println(relayOFF);
    f.flush();
    f.close();
    Serial.println(F("Saved values."));
}


/* Web Client
 * Listening for new clients & serve them
 */
void webClient() {
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


/*
 * updateLocalTimeRelay
 */
void updateLocalTimeRelay(boolean updateDisplay) {
    if(timeStatus() == timeSet) {
        local = myTZ.toLocal(now(), &tcr);
        sprintf(stringTime, "%02d:%02d", hour(local), minute(local));
        if(oldmin != minute()){
            oldmin = minute();
            if(updateDisplay) {
                display.setTextSize(2);
                display.setCursor(0,0);
                display.print(stringTime);
                display.display();
            }
        }
        // relay control
        if(currentMillis - relayMillis > relayInterval) {
            relayMillis = currentMillis;
            updateRelay(updateDisplay);
        }
        if (relayON == (hour(local) * 100 + minute(local))){
            relayState = true;
            updateRelay(updateDisplay);
        }
        if (relayOFF == (hour(local) * 100 + minute(local))){
            relayState = false;
            updateRelay(updateDisplay);
        }
    } else {
        sprintf(stringTime, "%s", defaultTime);
    }
}


/*
 * Setup
 */
void setup() {
    Serial.println(F("Boot Start."));

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, !relayState); // reverse logic for relay
    pinMode(resetButton, INPUT_PULLUP); // tempAlarm reset


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


    // get WiFi up and going
	Serial.println("");
	Serial.println(F("Connecting to: "));
    Serial.print(ssid);
    display.setTextSize(1);
    display.println(F("Connecting to: "));
    display.println(ssid);
    display.display();
    if (!initWifi(5, 10)) {
        Serial.println(F("Initial Connection Failed! Rebooting..."));
        display.println(F("Initial Connection Failed!"));
        display.println(F("Rebooting..."));
        display.display();
        delay(5000);
        ESP.restart();
    }
    Serial.println(F("Connected."));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
    display.println(F("Connected."));
    display.print(F("IP: "));
    display.println(WiFi.localIP());
    display.display();


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
    sprintf(stringTime, "%s", defaultTime);
    Udp.begin(localPort);
    setSyncProvider(getNtpTime);
    setSyncInterval(300);

    // MQTT
    mqttClient.setServer(mqtt_server, mqtt_serverport);
    mqttClient.setCallback(mqttCallback);
    if(initMQTT()) {
        display.setTextSize(1);
        display.println(F("MQTT connected."));
        display.display();
    } else {
        display.setTextSize(1);
        display.println(F("MQTT failed."));
        display.display();
    }

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
        tempLow = 69.50;
        tempHigh = 79.50;
        tempAlarm = 0;
        relayON = 0545;
        relayOFF = 2045; // defaults
    }
    while (f.available()) {
        String key = f.readStringUntil('=');
        String value = f.readStringUntil('\r');
        f.read();
        Serial.println(key + F(" = [") + value + ']');
        Serial.println(key.length());
        if (key == F("tempLow")) {
            tempLow = value.toFloat();
        }
        if (key == F("tempHigh")) {
            tempHigh = value.toFloat();
        }
        if (key == F("tempAlarm")) {
            tempAlarm = value.toInt();
        }
        if (key == F("relayON")) {
            relayON = value.toInt();
        }
        if (key == F("relayOFF")) {
            relayOFF = value.toInt();
        }
    }
    f.close();

    mqttRefreshConfig();

    // set relay once if time is in-between
    // from here on out will only be set on the minute
    // to allow for manual control
    // update display = false
    updateLocalTimeRelay(false);
    if(timeStatus() == timeSet) {
        if ((relayON <= (hour(local) * 100 + minute(local)))
            && (relayOFF >= (hour(local) * 100 + minute(local)))) {
            relayState = true;
        } else {
            relayState = false;
        }
        updateRelay(false);
    }

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

    // Wifi status & init if dropped
    display.setTextColor(WHITE, BLACK);
    display.setTextSize(1);
    display.setCursor(0,56);
    display.print("W:");
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println(F("Reconnecting WiFi."));
        display.print(message_status_server[0]);
        display.display();
        if(initWifi()) {
            Serial.println(F("WiFi connected."));
        }
        } else {
            display.print(message_status_server[1]);
            display.display();

            // MQTT status & init if dropped
            display.setCursor(96,56);
            display.print("M:");
            if(!mqttClient.connected()) {
                Serial.println(F("Reconnecting MQTT."));
                display.print(message_status_server[0]);
                display.display();
                if(initMQTT()) {
                    if(mqttRefreshConfig()) {
                        Serial.println(F("MQTT connected."));
                    }
                }
            } else {
                mqttClient.loop();
                display.print(message_status_server[1]);
                display.display();
            }
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

    // update Local Time & Relay
    updateLocalTimeRelay(true);

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
            display.print(message_status_tempalarm[tempAlarm]);
            display.print(F(":"));
            display.print(tempF,2);
            display.print(F("F"));
            display.display();

            if(mqttClient.connected()) {
                const size_t capacity = JSON_OBJECT_SIZE(2);
                StaticJsonDocument<capacity> doc;
                JsonObject obj = doc.createNestedObject("DS18B20");
                obj["Temperature"] = tempF;
                serializeJson(doc, buffer);
                mqttClient.publish(topic_status_temp, buffer, true);
            }
        }
        if(tempF > tempHigh){
            tempAlarm = 2; // set high alarm
            if(mqttClient.connected()) {
                mqttClient.publish(topic_status_tempalarm, message_status_tempalarm[tempAlarm], true);
            }
        } else if(tempF < tempLow){
            tempAlarm = 1; // set low alarm
            if(mqttClient.connected()) {
                mqttClient.publish(topic_status_tempalarm, message_status_tempalarm[tempAlarm], true);
            }
        }
    }

    // Web Client
    // Listening for new clients & serve them
    webClient();

    // update the config file if required
    if(cfgChangeFlag) {
            saveConfig();
            cfgChangeFlag = false;
        }
}
