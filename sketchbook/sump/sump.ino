/*********
	Sump Level

	update SeJ 03 25 2020 SeJ init
*********/

#include <../../../../../../../../../Projects/keys/sej/sej.h>
#include <ESP8266WiFi.h>

// Web Server on port 80
WiFiServer server(80);

const char* heartbeatresource = "/rest/vars/set/2/42/";
float heartbeat=0; // heartbeat to ISY


// time
unsigned long getisyMillis = 0;
unsigned long resetwifiMillis = 0;
long getisyInterval = 40000;
long resetwifiInterval = 60000;
char outstr[7];
int sump = D2;
int sumpval = 0;

/*
 * Setup
 */
void setup(){
    pinMode(sump, INPUT_PULLUP);
	Serial.begin(115200);
	delay(10);

	initWifi();
}

void loop(){
	unsigned long currentMillis = millis();
/*
 * Init Wifi if dropped
 */
if(currentMillis - resetwifiMillis > resetwifiInterval) {
    resetwifiMillis = currentMillis;
    if(heartbeat == 0){
        heartbeat = 1;
    }
    else {
        heartbeat = 0;
    }
    makeHTTPRequest(heartbeatresource,heartbeat);
    if(WiFi.status() != WL_CONNECTED) {
        initWifi();
    }
}

if(WiFi.status() == WL_CONNECTED) {
    /*
     * WebClient
     */
    // Listenning for new clients
    WiFiClient client = server.available();
    if (client) {
        Serial.println("New client");
        // bolean to locate when the http request ends
        boolean blank_line = true;
        while (client.connected()) {
            if (client.available()) {
                sumpval = digitalRead(sump);
                char c = client.read();
                if (c == '\n' && blank_line) {
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close");
                    client.println();
                    client.println("<!DOCTYPE HTML>");
                    client.println("<html>");
                    client.println("<head></head><body><h1>Jenkins Sump Level</h1>");
                    client.println("<h3>Sump Levl: ");
                    client.println(sumpval);
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
}

// Subroutines:

// Establish a Wi-Fi connection with your router
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
