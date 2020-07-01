/*
 *  Awning control & Living room temperature
 *
 *  Written for an ESP8266 TODO insert relay board here
 *  --fqbn esp8266:8266:??? TODO
 *
 *  init   SeJ 03 09 2019 specifics to my application
 *  update SeJ 03 24 2019 change to relay
 *  update SeJ 04 07 2019 changes to work on ESP relay
 *  update SeJ 06 30 2020 add MQTT capability -- REFER: [[https://gist.github.com/boverby/d391b689ce787f1713d4a409fb43a0a4][ESP8266 MQTT example]]
*/

#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

/* Passwords & Ports
* wifi: ssid, password
* ISY: hash, isy, isyport
* MQTT mqtt_server, mqtt_serverport
*/
#include <../../../../../../../../../Projects/keys/sej/sej.h>


// Web Server on port 80
WiFiServer server(80);
String ServerTitle = "Jenkins Living Room Awning";

// ISY
const char* tempresource = "/rest/vars/set/2/69/"; // NOTE ISY state temp variable
const char* awningcontrolresource = "/rest/vars/set/2/71/"; // NOTE ISY state control variable
const char* heartbeatresource = "/rest/vars/set/2/70/"; // NOTE ISY state hb variable
float heartbeat=0; // heartbeat to ISY

// MQTT
const char* topic = "sej"; // NOTE main topic
String clientId = "docktemp"; // NOTE client ID for this unit
const char* topic_temp = "sej/docktemp/temp"; // NOTE temp topic
const char* topic_hb = "sej/docktemp/hb"; // NOTE hb topic
char hb_send[4];

WiFiClient espClient;
PubSubClient mqttClient(espClient);
long mqttLastMsg = 0;
int mqttValue = 0;

// Data wire is plugged into pin D1 on the ESP8266 12-E - GPIO 5
#define ONE_WIRE_BUS 5

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature DS18B20(&oneWire);
char temperatureCString[7];
char temperatureFString[7];
char outstr[7];
float tempC;
float tempF;
float tempOld;

// time
unsigned long currentMillis;
unsigned long tempMillis = 0;
unsigned long getisyMillis = 0;
unsigned long resetwifiMillis = 0;
unsigned long tempInterval = 10000;
unsigned long getisyInterval = 40000;
unsigned long resetwifiInterval = 60000;

unsigned long awningcontrolMillis = 0;
unsigned long awningcontrolInterval = 30000;
int awningpushtime = 2000;
int control = true; // allow awning control periodically
int val = false; // valid http value received

// pins
int pbpower = 12; // Pin D2 GPIO 0 12
int pbbutton = 13; // Pin D3 GPIO 4 13


/*
 * Setup
 */
void setup(){
	Serial.begin(115200);
	delay(10);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH

    // IC Default 9 bit. If you have troubles consider upping it 12.
    // Ups the delay giving the IC more time to process the temperature measurement
	DS18B20.begin();

    initWifi();
    mqttClient.setServer(mqtt_server, mqtt_serverport);
    mqttClient.setCallback(mqttCallback);
	getTemperature();
    tempOld = 0;

    control = true;

    // prepare GPIO4
  pinMode(pbpower, OUTPUT);
  digitalWrite(pbpower, LOW);
  delay(1);
  pinMode(pbbutton, OUTPUT);
  digitalWrite(pbbutton, LOW);
  delay(1);
}

/*
 * Main Loop
 */
void loop(){
    // Init Wifi if dropped
    if(WiFi.status() != WL_CONNECTED) {
        initWifi();
    }

    // Init MQTT if dropped
    if(mqttClient.connected()) {
        mqttClient.loop();
    } else {
        mqttReconnect();
    }

    currentMillis = millis();

    // Temperature retrieve
    if(currentMillis - tempMillis > tempInterval) {
        tempMillis = currentMillis;
        getTemperature();
    }

    // Temperature to ISY
    if(currentMillis - getisyMillis > getisyInterval){
        getisyMillis = currentMillis;
        if (tempOld != tempF){
            makeHTTPRequest(tempresource,tempF);
            if(mqttClient.connected()) {
                mqttClient.publish(topic_temp, temperatureFString, true);
            }

            Serial.print("Updating ISY with ");
            Serial.println(temperatureFString);
        }
    }

    // Heartbeat to ISY
    if(currentMillis - resetwifiMillis > resetwifiInterval) {
        resetwifiMillis = currentMillis;
        if(heartbeat == 0){
            heartbeat = 1;
        }
        else {
            heartbeat = 0;
        }
        makeHTTPRequest(heartbeatresource,heartbeat);
        if(mqttClient.connected()) {
            dtostrf(heartbeat, 2, 0, hb_send);
            mqttClient.publish(topic_hb, hb_send, true);
        }
    }

    // Awning Control only so often
    if(currentMillis - awningcontrolMillis > awningcontrolInterval) {
        control = true;
    }

    val = false;
    // Listening for new clients
    WiFiClient client = server.available();
    if (client) {
        Serial.println("New client");

        // Read the first line of the request
        String s = client.readStringUntil('\r');
        Serial.println(s);
        client.flush();

        // Match the request
        if (s.indexOf("/awning/0") != -1) {
            val = true;
            Serial.print("request awning status: ");
            Serial.println(( (control) ? "true" : "false"));
        } else if (s.indexOf("/awning/1") != -1) {
            val = true;
            AwningControl();
            }

        // Respond to HTTP GET
        if(val){
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");
            client.println();
            // your actual web page that displays temperature
            client.println("<!DOCTYPE HTML><html><head></head><body><h1>");
            client.println(ServerTitle);
            client.println("</h1><h3>Awning control is now ");
            client.println((control) ? "true" : "false");
            client.println("</h3><h3>Temperature in Celsius: ");
            client.println(temperatureCString);
            client.println("*C</h3><h3>Temperature in Fahrenheit: ");
            client.println(temperatureFString);
            client.println("*F</h3></body></html>");
        }

        // closing the client connection
        delay(1);
        client.flush();
        client.stop();
        Serial.println("WebClient disconnected");
        val = false;
    }
}


// Subroutines:

/*
 * Establish Wi-Fi connection & start web server
 */
void initWifi() {
	Serial.println("");
	Serial.print("Connecting to: ");
	Serial.print(ssid);
	WiFi.begin(ssid, password);

	int timeout = 25 * 4; // 25 seconds
	while(WiFi.status() != WL_CONNECTED  && (timeout-- > 0)) {
		delay(250);
		Serial.print(".");
	}
	Serial.println("");

	if(WiFi.status() != WL_CONNECTED) {
		 Serial.println("WiFi Failed to connect");
	}
	else {
	Serial.print("WiFi connected in: ");
	Serial.print(millis());
	Serial.print(", IP address: ");
	Serial.println(WiFi.localIP());

	// Starting the web server
	server.begin();
	Serial.println("Web server running....");
	}
}


/*
 * MQTT client reconnect
 */
void mqttReconnect() {
    int timeout = 5 * 4; // 5 seconds
    while (!mqttClient.connected() && (timeout-- > 0)) {
        Serial.print("Attempting MQTT connection...");

        // Attempt to connect
        if (mqttClient.connect(clientId.c_str())) {
            Serial.println("connected");
            // Once connected, publish an announcement...
            mqttClient.publish(topic, ("connected " + clientId).c_str() , true );
            // ... and resubscribe
            // topic + clientID + in
            String subscription;
            subscription += topic;
            subscription += "/";
            subscription += clientId ;
            subscription += "/awningcontrol";
            mqttClient.subscribe(subscription.c_str() );
            Serial.print("subscribed to : ");
            Serial.println(subscription);
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.print(" wifi=");
            Serial.println(WiFi.status());
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
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    // Signal Awning if an 1 was received as first character
    if ((char)payload[0] == '1') {
        AwningControl();
    }
}


/*
 * Awning signal control
 */
void AwningControl() {
    Serial.println("request awning control");
    if(control == true){
        digitalWrite(pbpower, HIGH);
        digitalWrite(pbbutton, HIGH);
        Serial.println("**awning button pushed**");
        delay(awningpushtime);
        digitalWrite(pbbutton, LOW);
        digitalWrite(pbpower, LOW);
        Serial.println("**awning button released**");
        awningcontrolMillis = currentMillis;
        control = false;
    }
}


/*
 * GetTemperature from DS18B20
 */
void getTemperature() {
    do {
            DS18B20.requestTemperatures();
            tempC = DS18B20.getTempCByIndex(0);
            dtostrf(tempC, 2, 2, temperatureCString);
            tempF = DS18B20.getTempFByIndex(0);
            dtostrf(tempF, 3, 2, temperatureFString);
            delay(100);
        } while (tempC == 85.0 || tempC == (-127.0));
        Serial.print(ServerTitle);
        Serial.print("Temperature in Celsius: ");
        Serial.print(temperatureCString);
        Serial.print("   Temperature in Fahrenheit: ");
        Serial.println(temperatureFString);
    }


/*
 * Make an HTTP request to ISY
 */
void makeHTTPRequest(const char* resource, float data) {
    Serial.print("Connecting to ");
    Serial.print(isy);

    WiFiClient client;
    int retries = 5;
    while(!!!client.connect(isy, isyport) && (retries-- > 0)) {
        Serial.print(".");
    }
    Serial.println();
    if(!!!client.connected()) {
        Serial.println("Failed to connect, going back to sleep");
    }

    Serial.print("Request resource: ");
    Serial.println(resource);
    dtostrf(data, 3, 2, outstr);
    client.print(String("GET ") + resource + outstr +
                 " HTTP/1.1\r\n" +
                 "Host: " + isy + "\r\n" +
                 "Connection: close\r\n" +
                 "Content-Type: application/x-www-form-urlencoded\r\n" +
                 "Authorization: Basic " + hash + "\r\n\r\n");

    Serial.println("request sent");
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            Serial.println("headers received");
            break;
        }
    }
    String line = client.readStringUntil('\n');
    if (line.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\"?><RestResponse succeeded=\"true\"><status>200</status></RestResponse>")) {
        Serial.println("write to ISY successful!");
    } else {
        Serial.println("write to ISY has failed");
    }
    Serial.println("reply was:");
	Serial.println("==========");
	Serial.println(line);
	Serial.println("==========");
	Serial.println("closing connection");
	client.stop();
}
