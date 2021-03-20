#include "user_secrets.h"
#include <Wire.h>
#include <Adafruit_BME280.h>
#define SEALEVELPRESSURE_HPA (1013.25)
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

const int SENSOR_DATA_MQQT_TIMER_MS = 60000;
const int KEEP_GATE_RELAY_ON_FOR_MS = 300;
const int INPUTPIN_TRIGGER_COOLDOWN_MS = 1000;

//WIFI settings
const char* ssid     = SECRET_SSID; // "LMT-B830";
const char* password = SECRET_PSSWD;

//Enter your mqtt server configurations
const char* mqttServer = SECRET_MQTT_SERVER_ADRESS; 
const int mqttPort = 1883;       //Port number
const char* mqttUser = SECRET_MQTT_USER;
const char* mqttPassword = SECRET_MQTT_PSSWD;
long mqttConnectCooldown = 0L;

//System timer for all places that need time at current loop
long timeNow = 0L;

//For work with BME sensor
bool useBme = false;
Adafruit_BME280 bme;
float bme_temprature = -1.79f;
float bme_pressure = 753.06f;
float bme_humidity = 77.28f;
long bme_reportTimer = 0L;

//PIN definitions
const int buttonPin = 0;
const int PIN_GATERELAY = 14;
const int r1Pin = 12;

//State of relays
bool gateRelayState = false;
long timeGateRelayTouched = 0L;

bool inputPinState = false;
bool inputPinTriggerReported = true;
long timeInputPinTriggered = 0L;

unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

String header;
// Auxiliar variables to store the current output state
String output5State = "off";
String output4State = "off";

WiFiServer server(80);

WiFiClient espClient;
PubSubClient mqttclient(espClient);

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
 
  Serial.print("Message:");

  String message;
  for (int i = 0; i < length; i++) {
    message = message + (char)payload[i];  //Conver *byte to String
  }
  Serial.println(message);
  if(message == "#on") {digitalWrite(LED_BUILTIN,LOW);}   //LED on  
  if(message == "#off") {digitalWrite(LED_BUILTIN,HIGH);} //LED off
  
  if (strcmp(topic,"homer/homergate/cmd/r1")==0)
  {    
    if(message == "#on") {turnR1On();}   //LED on  
    if(message == "#off") {turnR1Off();} //LED off    
  }
  
  Serial.println("-----------------------");  
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("start!");
  
  pinMode(buttonPin, INPUT);  
  digitalWrite(buttonPin, HIGH);
  pinMode(PIN_GATERELAY, OUTPUT);
  digitalWrite(PIN_GATERELAY, !gateRelayState);
  pinMode(r1Pin, OUTPUT);
  digitalWrite(r1Pin, HIGH);

  if (useBme && !bme.begin(&Wire)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    useBme = false;
    //while (1);
  }

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();

  mqttclient.setServer(mqttServer, mqttPort);
  mqttclient.setCallback(MQTTcallback);

  reconnect_mqtt();
  Serial.println("MQTT init done");
}

///
/// Give signal to gate switch
///
void toggleGate() {
  if (!gateRelayState) {
    Serial.println("GPIO 5 on");
    output5State = "on";
  
    gateRelayState = true;
    timeGateRelayTouched = millis();      
    digitalWrite(PIN_GATERELAY, !gateRelayState);
    mqttclient.publish("homer/homergate/gate", "on");
  }
}

void turnR1Off() {
  Serial.println("R1 off");
  output4State = "off";
  digitalWrite(r1Pin, HIGH);  
  mqttclient.publish("homer/homergate/r1", "off");  
}

void turnR1On() {
  Serial.println("R1 on");
  output4State = "on";
  digitalWrite(r1Pin, LOW);
  mqttclient.publish("homer/homergate/r1", "on");  
}

///
/// On/Off relay 1
///
void toggleR1() {
  //šobrīd ir off - iesledzam, citadi izsledzam
  if (output4State == "off") 
  {
    turnR1On();
  }  
  else
  {
    turnR1Off();
  } 
}

void reconnect_mqtt() {
  if (!mqttclient.connected() && ((millis() - mqttConnectCooldown) > 2000L)) 
  {
    Serial.println("Attempting MQTT connection...");
    if (mqttclient.connect(mqttUser, mqttUser, mqttPassword )) { 
      Serial.println("connected");   
      mqttclient.publish("homer/register", "homergate");
      mqttclient.subscribe("homer/homergate/cmd/#");
    } else { 
      Serial.print("failed with state ");
      Serial.println(mqttclient.state());  //If you get state 5: mismatch in configuration            
    }
    mqttConnectCooldown = millis();
  }  
}

//tmp buf for use in flot->char* data conversions
static char outstr[15];

void loop() {
  timeNow = millis();
  
  reconnect_mqtt();  
  
  /// Read and report latest data  
  if (timeNow - bme_reportTimer > SENSOR_DATA_MQQT_TIMER_MS)
  {
    if (useBme)
    {
      bme_temprature = bme.readTemperature();
      bme_pressure = (float)bme.readPressure() * 0.00750062;
      bme_humidity = bme.readHumidity();
    }
    bme_reportTimer = timeNow;
    
    dtostrf(bme_temprature,7, 2, outstr);    
    mqttclient.publish("homer/homergate/temperature", outstr);    

    dtostrf(bme_pressure,7, 2, outstr); 
    mqttclient.publish("homer/homergate/pressure", outstr);

    dtostrf(bme_humidity,7, 2, outstr); 
    mqttclient.publish("homer/homergate/humidity", outstr);
  }

  if (gateRelayState && (timeNow - timeGateRelayTouched > KEEP_GATE_RELAY_ON_FOR_MS)) {
    gateRelayState = false;
    digitalWrite(PIN_GATERELAY, !gateRelayState);
  }

  //
  inputPinState = digitalRead(buttonPin);
  if (inputPinState == LOW) {
    // turn LED on:
    if (!inputPinTriggerReported)     
    {
      Serial.println("Input triggered!");
      mqttclient.publish("homer/homergate/input1", "on");      
      inputPinTriggerReported = true;
    }
    timeInputPinTriggered = timeNow;
  }
  else 
  {
     if (inputPinTriggerReported && (timeNow - timeInputPinTriggered > INPUTPIN_TRIGGER_COOLDOWN_MS))
     {
       inputPinTriggerReported = false;
     }
  }

  WiFiClient client = server.available();   // Listen for incoming clients
  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port

    if (useBme)
    {
      bme_temprature = bme.readTemperature();
      bme_pressure = (float)bme.readPressure() * 0.00750062;
      bme_humidity = bme.readHumidity();
    }
    
    String currentLine = "";                // make a String to hold incoming data from the client
    String postData = "";
    currentTime = timeNow;
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) // loop while the client's connected
    { 
      currentTime = millis();         
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        // if the byte is a newline character
        // if the current line is blank, you got two newline characters in a row.
        // that's the end of the client HTTP request head, so send a response:
        if (c == '\n') 
        {
          //if header says it's POST, then there is still POST data to read
          if (header.indexOf("POST") >= 0) 
          {
            Serial.println("post");
            while(client.available())
            {
              c = client.read();
              postData += c;
              Serial.write(c);
            }
            Serial.println();
            if (postData.indexOf("datavalue=") >= 0) 
            {
              if (postData.indexOf("gate") >= 0) {
                toggleGate();
              }
              if (postData.indexOf("r1") >= 0) {
                toggleR1();
              }
              if (postData.indexOf("gtest") >= 0) {
                //just for easy testing if mq works
                mqttclient.publish("homer/sensor/irgate", "on");
              }
            }
            // Post processed lets get out of here
            break;
          }
                    
          if (currentLine.length() == 0) 
          {
            printPageContent(client);
            // Break out of the while loop
            break;
          } 
          else
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    postData = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
  
}

void printPageContent(WiFiClient client) {
 // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style>");

            client.print("<script type=\"text/javascript\">\r\n");
            client.print("function button_event(e, dval)\r\n");
            client.print("{\r\n");            
            client.print("var request = new XMLHttpRequest();\r\n");
            client.print("request.open(\"POST\",\"");
            client.print(WiFi.localIP()); client.print("\");\r\n");
            client.print("request.setRequestHeader(\"Content-Type\", \"application/x-www-form-urlencoded\");\r\n");
            client.print("request.send(\"datavalue=\" + dval);\r\n");              
            client.println("setTimeout(function () {");
            client.println("location.reload();");
            client.println("}, 1000);");
            client.println("e.style.backgroundColor=\"green\";");
            client.println("}");
            client.print("</script>\r\n");

            client.println("</head>");
            
            // Web Page Heading
            client.println("<body>"); //<h1>Homer gate</h1>");
            client.println("<div><h2>Temp:</h2>");
            client.println(bme_temprature);
            client.println("</div>");
            client.println("<div><h2>Pressure:</h2>");
            client.println(bme_pressure);
            client.println("</div>");
            if (useBme)
            {
              client.println("<div><h2>Altitude:</h2>");
              client.println(bme.readAltitude(SEALEVELPRESSURE_HPA));
            }
            client.println("</div>");
            client.println("<div><h2>Humidity:</h2>");
            client.println(bme_humidity);
            client.println("</div>");   
            
            //GATE                                                
            client.println("<p><button id = \"btnGate\" type=\"button\" class=\"button\" onclick=\"button_event(this, 'gate')\">GATE</button></p>");
            client.println("<p>R1 - State " + output4State + "</p>");
            client.println("<p><button id = \"btnR1\" type=\"button\" class=\"button\" onclick=\"button_event(this, 'r1')\">");            
            if (output4State=="off") {
              client.print("TURN ON");
            } else {
              client.print("TURN OFF");
            }
            client.println("</button></p>");            
            client.println("<p><button id = \"btnGt\" type=\"button\" class=\"button\" onclick=\"button_event(this, 'gtest')\">");
            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
}

