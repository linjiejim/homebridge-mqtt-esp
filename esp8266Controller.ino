/*
COMMEND：
  5/8: client will set NO RESPONSE to home when it was offline. 
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Timer.h"

//Pins
#define SWITCH_PIN 5  //D1          //high to turn on
#define BULB_PIN 14   //D5          //high to light up
#define STATUS_LED_PIN 2  //D4      //low to light up

//Network CONFIG
const char* ssid = "209wifi";
const char* password = "209209209";
const char* mqtt_server = "192.168.31.31";
const int mqtt_server_port = 1883;

//Homebridge-mqtt INFO
const char* accessory_name = "espController";   //general device
const char* service_switch = "espSwitch";         //sub device
const char* service_bulb = "espBulb";           //sub device

// Mosquitto TOPIC
const char* initTopic = "homebridge/to/get";         //publish
const char* subTopic = "homebridge/from/set";        //subscribe
const char* respTopic = "homebridge/from/response";
const char* reachableTopic = "homebridge/to/set/reachability";
const char* unreachablePayload = "{\"name\": \"espController\", \"reachable\": false}";      //!!! HOW to init ESP_NAME with accessory_name???
const char* reachablePayload = "{\"name\": \"espController\", \"reachable\": true}";

//Global Variables
long lastTime = 0;                //timer
long nowTime = 0;
bool switchStatus = false;        //status
bool bulbStatus = false;
bool recStatusFlag = false;
int bulbBrightness = 50;
char payload[250];                //payload

//Objects
WiFiClient espClient;             //connect to wifi
PubSubClient client(espClient);  //mqtt client 
Timer timer;

void setup() {
  /* setup pin mode */
  pinMode(SWITCH_PIN, OUTPUT);
  pinMode(BULB_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  /* setup function */
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_server_port);
  client.setCallback(callback);

  retrieveValue();  //get value from homebridge and setup
  
  delay(1000);    // Allow the hardware to sort itself out

  debugging("System", "Running", "Setup done, start working! ");
  
  /* set cyclical functiom with timer */
  //timer.every(5000, function);
}

void loop() {
  //Device offline?
  if (!client.connected()){ reconnect(); }
  
  client.loop();

  timer.update();
  
}

// retrieve value of accessory from homebridge when setup
void retrieveValue(){
  // wait untill received response
  while(recStatusFlag == false){
    if (!client.connected()){ 
      reconnect(); 
    }
   
    snprintf (payload, sizeof(payload), "{\"name\": \"%s\" }", accessory_name);
    client.publish(initTopic, payload);
    debugging("Initial", initTopic, payload);
    
    delay(1000);

    client.loop();
  }
  recStatusFlag = false;
  client.unsubscribe(respTopic);
}

// get accessory status from homebridge and init
bool initController(byte* json, unsigned int len){
  const int capacity = 2*JSON_OBJECT_SIZE(1) 
                      + 4*JSON_OBJECT_SIZE(2);
  StaticJsonBuffer<capacity> jb;
  JsonObject& root = jb.parseObject(json);

  // Test if parsing succeeds.
  if (!root.success()) {
    debugging("Json", "Error", "Fail to parse Json");
    return false;
  }

  //NOT for this accessory
  /*if(root[accessory_name] == NULL ){
    return false;
  }*/
  
  bool bulbStat = root[accessory_name]["characteristics"][service_bulb]["On"];
  int  bulbBrig = root[accessory_name]["characteristics"][service_bulb]["Brightness"];
  bool switchStat = root[accessory_name]["characteristics"][service_switch]["On"];

  // init bulb
  if(bulbStat == true){
    analogWrite(BULB_PIN, bulbBrig);
  }
  else{
    digitalWrite(BULB_PIN, LOW);
  }

  // init switch 
  digitalWrite(SWITCH_PIN, switchStat);

  return true;
}

// set controller status
bool setController(byte* json, unsigned int len){
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);

  // Test if parsing succeeds.
  if (!root.success()) {
    debugging("Json", "Error", "Fail to parse Json");
    return false;
  }
  
  // Fliter
  String name = root["name"];
  if(name.equals(accessory_name) ){
    
    //foreach service
    String service_name = root["service_name"];
    String characteristic = root["characteristic"];
    String value = root["value"];
    /* bulb */
    if(service_name.equals(service_bulb) ){
      
      //DO SOMETHING refer to value
      if(characteristic.equals("On") ){
        if(value.equals("true") ){
          analogWrite(BULB_PIN, map(bulbBrightness, 0, 100, 0, 255) );
          debugging("Bulb", "On", "Bulb on" );
        }else{
          digitalWrite(BULB_PIN, LOW);
          debugging("Bulb", "Off", "Bulb off" );
        }
      }
      else if(characteristic.equals("Brightness") ){
        bulbBrightness = value.toInt();
        analogWrite(BULB_PIN, map(bulbBrightness, 0, 100, 0, 255) );
        debugging("Bulb", "Brightness", String(bulbBrightness) );
      }
      else{
        debugging("Bulb", "Error", "Fail to validate the value");
        return false;
      }
      return true;
    }
    /* switch */
    else if(service_name.equals(service_switch)){
  
      //DO SOMETHING refer to value
      if(value.equals("true") ){
        digitalWrite(SWITCH_PIN, HIGH);
        debugging("Switch", "Status", "On");
      }else if(value.equals("false") ){
        digitalWrite(SWITCH_PIN, LOW);
        debugging("Switch", "Status", "Off");
      }
      else{
        debugging("Switch", "Error", "Fail to validate the value");
        return false;
      }
    }
  }
  //debugging("Received Unknown Message", "Warning", "Not for this accessory or service" );
  return false;
}

//println debugging details to serial
void debugging(String title, String topic, String message){
  Serial.print(title);
  Serial.print(" [ ") ;
  Serial.print(topic);
  Serial.print(" ] ");
  Serial.println(message);
}

//reconnect to mqtt server
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    //if (client.connect(accessory_name)) {
    if (client.connect(accessory_name, reachableTopic, 2, false, unreachablePayload)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(reachableTopic, reachablePayload);      //set reachable?
      
      // ... and resubscribe
      client.subscribe(subTopic);
      client.subscribe(respTopic);
      
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
      delay(1000);
      
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");

      digitalWrite(STATUS_LED_PIN, HIGH);
      delay(1000);
    }
  }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(250);
    
    Serial.print(".");
    
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(250);
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

//CALL this function when recive a mqtt message
void callback(char* topic, byte* payload, unsigned int length) {
  //Serial.println("callback called");
  
  digitalWrite(STATUS_LED_PIN, LOW);

  if(strcmp(topic, respTopic) == 0){    //response Topic
    recStatusFlag = true; 
    initController(payload, length);
  }
  else if(strcmp(topic, subTopic) == 0){  //set controller Topic
    setController(payload, length);
  }
  else{                                   //other Topic
    // do nothing
    /*///TEST 
    Serial.print("[");
    Serial.print(topic);
    Serial.print("]");
    Serial.println((char*)payload);
    //TEST */
  }
  
  
  digitalWrite(STATUS_LED_PIN, HIGH);
}

