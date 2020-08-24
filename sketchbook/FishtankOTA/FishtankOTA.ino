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

// MQTT
const char* topic = "sej"; //  main topic
String clientId = "fishtank"; // client ID for this unit

const char* topic_temp = "sej/fishtank/status/temp"; // temp topic

const char* topic_control = "sej/fishtank/control/relay"; // control topic
const char* control_message[] = {"ON", "OFF"};

const char* topic_monitor = "sej/fishtank/status/relay"; // monitor topic
const char* monitor_message[] = {"ON", "OFF"};

const char* topic_hb = "sej/fishtank/status/hb"; // hb topic
const char* hb_message[] = {"ON", "OFF"};

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

// time
unsigned long currentMillis = 0;
unsigned long tempMillis = 0;
const long tempInterval = 30000; // minimum 10s for DS18B20
unsigned long mqttMillis = 0;
const long mqttInterval = 30000;
unsigned long hbMillis = 0;
const long hbInterval = 60000;
unsigned long ledMillis = 0;
const long ledInterval = 1000;
bool ledState = false;
unsigned long relayMillis = 0;
const long relayInterval = 30000;
bool relayState = false;

// IO
// LED_BUILTIN is D0 on ESP-12E
int relay = D6; // power relay

void setup() {
    Serial.println("Boot Start.");

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(relay, OUTPUT);

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
    initWifi();
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Initial Connection Failed! Rebooting...");
        display.println("Initial Connection Failed!");
        display.println("Rebooting...");
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
                Serial.println("Auth Failed");
            } else if (error == OTA_BEGIN_ERROR) {
                Serial.println("Begin Failed");
            } else if (error == OTA_CONNECT_ERROR) {
                Serial.println("Connect Failed");
            } else if (error == OTA_RECEIVE_ERROR) {
                Serial.println("Receive Failed");
            } else if (error == OTA_END_ERROR) {
                Serial.println("End Failed");
            }
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready.");
    display.println("OTA Ready.");
    display.display();

    mqttClient.setServer(mqtt_server, mqtt_serverport);
    mqttClient.setCallback(mqttCallback);
    initMQTT();

    Serial.println("DS18B20 Start.");
    display.println("DS18B20 Start.");
    display.display();
    DS18B20.begin();
    getTemperature();
    tempOld = 0;

    Serial.println("Boot complete.");
    display.println("Boot Complete.");
    display.display();
    delay(1000);
    display.clearDisplay();
}


void loop() {
    ArduinoOTA.handle();

    currentMillis = millis();

    // Init Wifi if dropped
    if(WiFi.status() != WL_CONNECTED) {
        display.clearDisplay();
        display.setTextColor(WHITE, BLACK);
        display.setCursor(0,0);
        Serial.println("Reconnecting WiFi.");
        display.println("Reconnecting WiFi.");
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
        Serial.println("Reconnecting MQTT.");
        display.println("Reconnecting MQTT.");
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
            mqttClient.publish(topic_hb, hb_message[heartbeat] , true);
        }
    }

    // relay control
    if(currentMillis - relayMillis > relayInterval) {
        relayMillis = currentMillis;
        relayState = not(relayState);
        digitalWrite(relay, !relayState); // reverse logic for relay
        Serial.print("Relay: ");
        Serial.print(monitor_message[!relayState]);
        display.setTextSize(2);
        display.setCursor(0,48);
        display.print("relay:");
        display.print(monitor_message[!relayState]);
        display.print(" ");
        display.display();
        if(mqttClient.connected()) {
            mqttClient.publish(topic_monitor, monitor_message[!relayState], true);
        }
    }

    // Temperature retrieve & publish
    if(currentMillis - tempMillis > tempInterval) {
        tempMillis = currentMillis;
        getTemperature();
        if (tempOld != tempF && tempF > 0){
            Serial.print("Temp in Celsius: ");
            Serial.print(tempC,2);
            Serial.print("   Temp in Fahrenheit: ");
            Serial.println(tempF,2);

            display.setCursor(0,0);
            display.setTextSize(2);
            display.print("int:");
            display.print(tempF,2);
            display.print("F");
            display.display();

            if(mqttClient.connected()) {
                const size_t capacity = JSON_OBJECT_SIZE(2);
                StaticJsonDocument<capacity> doc;
                JsonObject obj = doc.createNestedObject("DS18B20");
                obj["Temperature"] = tempF;

                char buffer[256];
                serializeJson(doc, buffer);
                mqttClient.publish(topic_temp, buffer, true);
            }
        }
    }

    // Web Client
    // Listening for new clients
    WiFiClient client = server.available();
    if (client) {
        Serial.println("New client");
        // bolean to locate when the http request ends
        boolean blank_line = true;
        while (client.connected()) {
            if (client.available()) {
                char c = client.read();
                if (c == '\n' && blank_line) {
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close");
                    client.println();
                    // web page that displays temperature
                    client.println("<!DOCTYPE HTML><html><head></head><body><h1>");
                    client.println(ServerTitle);
                    client.println("</h1><h3>Temperature in Celsius: ");
                    client.println(tempC,2);
                    client.println("*C</h3><h3>Temperature in Fahrenheit: ");
                    client.println(tempF,2);
                    client.println("*F</h3><h3>Relay State: ");
                    client.println(relayState, BIN);
                    client.println("</h3></body></html>");
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
        Serial.println("WebClient disconnected.");
    }

}

// Subroutines

/*
 * Establish Wi-Fi connection & start web server
 */
void initWifi() {
	Serial.println("");
	Serial.println("Connecting to: ");
	Serial.print(ssid);
    display.setTextSize(1);
    display.println("Connecting to: ");
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
        Serial.println("Connected.");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        display.println("Connected.");
        display.print("IP: ");
        display.println(WiFi.localIP());
        display.display();

        // Starting the web server
        server.begin();
        display.display();
	}
}


/*
 * MQTT client init connection
 */
void initMQTT() {
    while (!mqttClient.connected() && ((currentMillis == 0) || (currentMillis - mqttMillis > mqttInterval))) {
        Serial.print("Attempting MQTT connection...");

        // Attempt to connect
        if (mqttClient.connect(clientId.c_str(), clientId.c_str(), password,
                               willTopic, willQoS, willRetain, willMessage)) {
            Serial.println("connected");
            display.setTextSize(1);
            display.println("MQTT connected.");
            display.display();
            // Once connected, publish an announcement...
            mqttClient.publish(topic, ("connected " + clientId).c_str() , true );
            mqttClient.subscribe(topic_control);
            Serial.print("subscribed to : ");
            Serial.println(topic_control);
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.print(" wifi=");
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
    if (strcmp((char *)mypayload, (char *)control_message[0]) == 0) {
        relayState = true;
        digitalWrite(relay, !relayState);   // relay is reverse logic
        mqttClient.publish(topic_monitor, monitor_message[0], true);
    } else if (strcmp((char *)mypayload, (char *)control_message[1]) == 0) {
        relayState = false;
        digitalWrite(relay, !relayState);  // relay is reverse logic
        mqttClient.publish(topic_monitor, monitor_message[1], true);
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
