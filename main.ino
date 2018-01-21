/*
  Generic ESP8266 Module
  Flash Mode: QIO
  Flash Frequency: 40 MHz
  CPU Frequency: 80 MHz
  Flash Size: 1M (256K SPIFFS)
  Debug Port: disabled
  Debug Level: none
  Reset Mode: ck
  Upload Speed: 115200
*/
#include <HLW8012.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define CALIBRATE_RUN 0       // do calibration to determine the inital values    enable = 1, disable = 0

#define RELAY_PIN     12
#define LED_PIN       15
#define BUTTON_PIN    0
#define SEL_PIN       5
#define CF1_PIN       13
#define CF_PIN        14

#define TIMER 15 //sec
#define UPDATE_TIME                     10000   // Check values every 10 seconds
#define CURRENT_MODE                    HIGH
// These are the nominal values for the resistors in the circuit
#define CURRENT_RESISTOR                0.001
#define VOLTAGE_RESISTOR_UPSTREAM       ( 5 * 470000 ) // Real: 2280k
#define VOLTAGE_RESISTOR_DOWNSTREAM     ( 1000 ) // Real 1.009k

HLW8012 hlw8012;

const char* ssid = "...";
const char* password = "...";
const char* mqtt_server = "192.168.1.190"; //Сервер MQTT

IPAddress ip(192, 168, 1, 63); //IP модуля
IPAddress gateway(192, 168, 1, 1); // шлюз
IPAddress subnet(255, 255, 255, 0); // маска
WiFiClient espClient;
PubSubClient client(espClient);

boolean run_cal =  CALIBRATE_RUN;
unsigned long prevMillis = 0;
int timer = TIMER;
static char buf [100];
bool old_state;

unsigned long lastmillis;
unsigned long buttontimer;
boolean wait_for_brelease = false;

void hlw8012_cf1_interrupt() {
  hlw8012.cf1_interrupt();
}
void hlw8012_cf_interrupt() {
  hlw8012.cf_interrupt();
}
void setInterrupts() {
  attachInterrupt(CF1_PIN, hlw8012_cf1_interrupt, CHANGE);
  attachInterrupt(CF_PIN, hlw8012_cf_interrupt, CHANGE);
}

void setup_wifi() {
  delay(10);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.config(ip, gateway, subnet);
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void reconnect() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  if (client.connect("iron")) {
    client.publish("myhome/iron/relay", IntToBool(digitalRead(RELAY_PIN)));
    client.publish("myhome/iron/timer", IntToChar(timer));
    client.publish("myhome/iron/active_power", IntToChar(hlw8012.getActivePower()));
    client.publish("myhome/iron/voltage", IntToChar(hlw8012.getVoltage()));
    client.publish("myhome/iron/current", IntToChar(hlw8012.getCurrent()));
    client.publish("myhome/iron/apparent_power", IntToChar(hlw8012.getApparentPower()));
    client.publish("myhome/iron/power_factor", IntToChar((int) (100 * hlw8012.getPowerFactor())));
    client.subscribe("myhome/iron/#");
    digitalWrite(LED_PIN, HIGH);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String strTopic = String(topic);
  String strPayload = String((char*)payload);
  ///////////
  if (strTopic == "myhome/iron/relay") {
    if (strPayload == "true") {
      timer = TIMER;
    } else if (strPayload == "false") {
      timer = 0;
    }
    client.publish("myhome/iron/relay", IntToBool(digitalRead(RELAY_PIN)));
  }
}

void setup() {
  Serial.begin(11520);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setup_wifi();
  digitalWrite(RELAY_PIN, HIGH);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  ArduinoOTA.onStart([]() {  });
  ArduinoOTA.onEnd([]() {  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {  });
  ArduinoOTA.onError([](ota_error_t error) {  });
  ArduinoOTA.begin();

  if (!run_cal) {
    hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, true);
    hlw8012.setCurrentMultiplier(15409.09);
    hlw8012.setVoltageMultiplier(431876.50);
    hlw8012.setPowerMultiplier(11791717.38);
  }
  if (run_cal) {
    hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, false, 500000);
    hlw8012.setResistors(CURRENT_RESISTOR, VOLTAGE_RESISTOR_UPSTREAM, VOLTAGE_RESISTOR_DOWNSTREAM);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    calibrate();
    //delay(60000);
    //ESP.restart();
  }
  setInterrupts();
}

void loop() {

  if (millis() - prevMillis >= 1000) {
    prevMillis = millis();
    timer = timer - 1;
    if (timer <= 0) { timer = 0; }
    if (digitalRead(RELAY_PIN) && client.connected()) {
        client.publish("myhome/iron/timer", IntToChar(timer));
    }
    if(timer == 0){
      digitalWrite(RELAY_PIN, LOW);
    } else {
      digitalWrite(RELAY_PIN, HIGH);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
    digitalWrite(LED_PIN, HIGH);
    if (!client.connected()) {
      reconnect();
    } else {
      client.loop();
      if (digitalRead(RELAY_PIN) != old_state) {
        old_state = digitalRead(RELAY_PIN);
        client.publish("myhome/iron/relay", IntToBool(digitalRead(RELAY_PIN)));
      }
      if (millis() - lastmillis > UPDATE_TIME) {
        client.publish("myhome/iron/active_power", IntToChar(hlw8012.getActivePower()));
        client.publish("myhome/iron/voltage", IntToChar(hlw8012.getVoltage()));
        client.publish("myhome/iron/current", IntToChar(hlw8012.getCurrent()));
        client.publish("myhome/iron/apparent_power", IntToChar(hlw8012.getApparentPower()));
        client.publish("myhome/iron/power_factor", IntToChar((int) (100 * hlw8012.getPowerFactor())));
        lastmillis = millis();
        hlw8012.toggleMode();
      }
    }
  }
  if (!digitalRead(BUTTON_PIN) && buttontimer == 0 && !wait_for_brelease) {
    buttontimer = millis();
  } else if (!digitalRead(BUTTON_PIN) && buttontimer != 0) {
    if (millis() - buttontimer > 20 && !wait_for_brelease) {
      timer = timer + TIMER;
      buttontimer = 0;
      wait_for_brelease = true;
    }
  }
  if (wait_for_brelease) {
    if (digitalRead(BUTTON_PIN)) wait_for_brelease = false;
  }
}

const char* IntToBool (int r) {
  if (r > 0) {
    return "true";
  } else {
    return "false";
  }
}

const char* IntToChar (unsigned int v) {
  sprintf(buf, "%d", v);
  return buf;
}

const char* FloatToChar (float f) {
  sprintf(buf, "%d.%02d", (int)f, (int)(f * 100) % 100);
  return buf;
}

void calibrate() {
  hlw8012.getActivePower();
  hlw8012.setMode(MODE_CURRENT);
  unblockingDelay(2000);
  hlw8012.getCurrent();
  hlw8012.setMode(MODE_VOLTAGE);
  unblockingDelay(2000);
  hlw8012.getVoltage();
  // Calibrate using a 60W bulb (pure resistive) on a 230V line
  hlw8012.expectedActivePower(60.0);
  hlw8012.expectedVoltage(238.9);
  hlw8012.expectedCurrent(60.0 / 238.9);
  // Show corrected factors
  client.publish("myhome/iron/calibrate/current_multiplier", FloatToChar(hlw8012.getCurrentMultiplier()));
  client.publish("myhome/iron/calibrate/voltage_multiplier", FloatToChar(hlw8012.getVoltageMultiplier()));
  client.publish("myhome/iron/calibrate/power_multiplier",   FloatToChar(hlw8012.getPowerMultiplier()));
}

void unblockingDelay(unsigned long mseconds) {
    unsigned long timeout = millis();
    while ((millis() - timeout) < mseconds) delay(1);
}
