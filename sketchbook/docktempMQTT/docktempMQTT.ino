/*
 *	Dock temperature
 *
 *	update SeJ 04 14 2018 specifics to my application
 *	update SeJ 04 21 2018 add password header & heartbeat
 *	update SeJ 04 28 2018 separate docktemp & pondtemp
 *  update SeJ 06 29 2020 add MQTT capability
*/

#include <../../../../../../../../../Projects/keys/sej/sej.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>


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

// Web Server on port 80
WiFiServer server(80);

// ISY
const char* tempresource = "/rest/vars/set/2/43/";
const char* heartbeatresource = "/rest/vars/set/2/44/";
float heartbeat=0; // heartbeat to ISY

// time
unsigned long tempMillis = 0;
unsigned long getisyMillis = 0;
unsigned long resetwifiMillis = 0;
unsigned long tempInterval = 10000;
unsigned long getisyInterval = 40000;
unsigned long resetwifiInterval = 60000;

// MQTT
const char* topic = "sej/docktemp";


/*
 * Setup
 */
void setup(){
	Serial.begin(115200);
	delay(10);

    pinMode(LED_BUILTIN, OUTPUT);

    // IC Default 9 bit. If you have troubles consider upping it 12.
    // Ups the delay giving the IC more time to process the temperature measurement
	DS18B20.begin();

	initWifi();
	getTemperature();
	tempOld = 0;
}


/*
 * Main Loop
 */
void loop(){
    // Init Wifi if dropped
    if(WiFi.status() != WL_CONNECTED) {
        initWifi();
    }

    unsigned long currentMillis = millis();

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
    }

    // Web Client

    // Listenning for new clients
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
                    // your actual web page that displays temperature
                    client.println("<!DOCTYPE HTML>");
                    client.println("<html>");
                    client.println("<head></head><body><h1>Jenkins Lake Temperature</h1><h3>Temperature in Celsius: ");
                    client.println(temperatureCString);
                    client.println("*C</h3><h3>Temperature in Fahrenheit: ");
                    client.println(temperatureFString);
                    client.println("*F</h3></body></html>");
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


// Subroutines:

/*
 * Establish Wi-Fi connection
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
	Serial.print("Jenkins Lake Temp:  ");
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
