/**************************************************************
 *
 * This script tries to auto-detect the baud rate
 * and allows direct AT commands access
 *
 * TinyGSM Getting Started guide:
 *   http://tiny.cc/tiny-gsm-readme
 *
 **************************************************************/
#define TINY_GSM_MODEM_A7
#define TINY_GSM_DEBUG Serial
#include <TinyGPS++.h>
#include <TinyGsmClient.h>
#include <Preferences.h>
#include "SimpleBLE.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

/* create an instance of Preferences library */
Preferences preferences;
SimpleBLE ble;

HardwareSerial SerialAT(2);
HardwareSerial SerialGPS(1);

String inputString = "";         // a String to hold incoming data
String sender = "";
String msg = "";
String myDate = "";
String myTime = "";
String myAge = "";
bool nextLineIsMsg = false;
String chipID = "";
int relay = LOW;

String boundTo;
bool unBound = true;
bool gpsSaved = false;

TinyGPSPlus gps;
TinyGPSPlus gpsold;
const int Relay = 5;
const int ModemPower = 18;
int sensorValue = 0;
// Module baud rate
uint32_t rate = 0; // Set to 0 for Auto-Detect
uint64_t chipid;


void setup() {
  pinMode(Relay, OUTPUT);
  pinMode(ModemPower, OUTPUT);

  // Set console baud rate
  Serial.begin(115200);
  Serial.println();
  SerialGPS.begin(9600);

  delay(1000);
  digitalWrite(Relay, LOW);

  Serial.println(F("Turn on Modem"));
  digitalWrite(ModemPower, HIGH);
  delay(2500);
  digitalWrite(ModemPower, LOW);

  chipid = ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32));//print High 2 bytes
  Serial.printf("%08X\n", (uint32_t)chipid);//print Low 4bytes.
    
  char sz[32] = { '\0'};
  sprintf(sz, "ESP32_%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  chipID = sz;
    
  initDevice(false);
  if (unBound) {
    ble.begin(chipID);
  }
}

void loop() {

  if (!rate) {
    rate = TinyGsmAutoBaud(SerialAT);
  }

  if (!rate) {
    Serial.println(F("***********************************************************"));
    Serial.println(F(" Module does not respond!"));
    Serial.println(F("   Check your Serial wiring"));
    Serial.println(F("   Check the module is correctly powered and turned on"));
    Serial.println(F("***********************************************************"));
    delay(300);
    return;
  }

  // Access AT commands from Serial Monitor
  Serial.println(F("***********************************************************"));
  Serial.println(F(" You can now send AT commands"));
  Serial.println(F(" Enter \"AT\" (without quotes), and you should see \"OK\""));
  Serial.println(F(" If it doesn't work, select \"Both NL & CR\" in Serial Monitor"));
  Serial.println(F("***********************************************************"));
  delay(5000);
  SerialAT.println(F("AT+CMGF=1"));
  delay(1000);
  SerialAT.println(F("AT+CPBS=?"));
  delay(1000);
  SerialAT.println(F("AT+GMI"));
  delay(1000);
  SerialAT.println(F("AT+GPS=1"));
  //  SerialAT.println(F("AT+AGPS=1"));
  delay(300);

  while (true) {

    // Read modem data
    if (SerialAT.available()) {
      inputString = SerialAT.readStringUntil('\n');
      Serial.print(F("From modem: "));
      Serial.println(inputString);
      if (nextLineIsMsg) {
        msg = inputString;
        nextLineIsMsg = false;
        delay(300);
        DisplaySMS();
        delay(1000);
        displayInfo();
        if (validateMsg()) {
          if (msg[0] == 'i') {
            initDevice(true);
            sendKey();
            ble.end();
          }
          else {
            if (sender == boundTo) {
              if (msg[0] == 'c') {
                if (gps.location.isValid()) {
                  CoordSMS(gps);
                } else {
                  if (gpsSaved) {
                    CoordSMS(gpsold);
                  } else {
                    // Send the invalid data
                    CoordSMS(gps);
                  }
                }
              }
              if (msg[0] == '1') { relayOn(); }
              if (msg[0] == '0') { relayOff(); }
              if (msg[0] == 's') { relayStatus(); }
              if (msg[0] == 'u') {
                unBind();
                sendDisconnect();
                delay(2000);
                ESP.restart();
              }
            } else {
              Serial.println(F("Unbound sender"));
            }
          }
        } else {
           Serial.println(F("Invalid message"));
        }

      }

      if (inputString.substring(0, 5) == "+CMT:") {
        Serial.println(F("GOT SMS"));
        sender = inputString.substring(7, 18);
        //Serial.println(sender);
        nextLineIsMsg = true;
      }

    }

    // Read GPS Data
    while (SerialGPS.available() > 0) {
      if (gps.encode(SerialGPS.read())) {
        preferences.begin("stopme", false);
        if (gps.location.isValid()) {
          Serial.println(F("Saving GPS"));
          preferences.putBool("gpsSaved", true);
          preferences.putBytes("gps", &gps, sizeof(gps));
        }
        preferences.end();
      }
    }

    // Echo any input on serial to the Thinker A7 
    if (Serial.available()) {
      SerialAT.write(Serial.read());
    }
    delay(0);
  }
}

void DisplaySMS()
{
  Serial.print(F("Got SMS From: "));
  Serial.print(sender);
  Serial.print(F(" Containing msg: "));
  Serial.println(msg);
}

void unBind() {
  preferences.begin("stopme", false);
  preferences.clear();
  preferences.end();
}

void initDevice(boolean update) {
  preferences.begin("stopme", false);
  boundTo = preferences.getString("boundTo", "");
  unBound = (boundTo == "");
  if (update) {
    Serial.println(F("initDevice(true)"));
    boundTo = sender;
    if (unBound) {
      preferences.putString("boundTo", sender);
    }
  } else {
    gpsSaved = preferences.getBool("gpsSaved", false);
    if (gpsSaved) {
      preferences.getBytes("gps", &gpsold, sizeof(gps));
      printDateTime(gps.date, gps.time);
      Serial.println(F("Last know location: "));
      displayLocation();
    }
  }
  Serial.print(F("Device bound to: "));
  Serial.println(boundTo);
  preferences.end();
}

void displayLocation() {
  Serial.print(F("{"));
  Serial.print(F("\"response\": \"location\","));
  Serial.print(F("\"lat\": "));
  Serial.print(gps.location.lat(), 6);
  Serial.print(", \"lng\": ");
  Serial.print(gps.location.lng(), 6);
  Serial.print(", \"time\": \"");
  Serial.print(myDate);
  Serial.print(myTime);
  Serial.print("\",\"age\": ");
  Serial.print(myAge);
  Serial.print("}\n");
}

void CoordSMS(TinyGPSPlus & gps)
{
  printDateTime(gps.date, gps.time);
  Serial.print(F("Sender response:"));
  displayLocation();

  SerialAT.println("AT+CMGS=" + sender);
  delay(100);
  SerialAT.print("{");
  SerialAT.print("\"response\": \"location\",");
  SerialAT.print("\"lat\": ");
  SerialAT.print(gps.location.lat(), 6);
  SerialAT.print(", \"lng\": ");
  SerialAT.print(gps.location.lng(), 6);
  SerialAT.print(", \"time\": \"");
  SerialAT.print(myDate);
  SerialAT.print(myTime);
  SerialAT.print("\",\"age\": ");
  SerialAT.print(myAge);
  SerialAT.print("}\032");
}

void printKey() {
  Serial.print(F("\"key\": \""));
  Serial.print(chipID);
  Serial.print(F("\""));
}

void sendKey()
{
  Serial.print(F("Sender response: "));
  Serial.print(F("{"));
  Serial.print(F("\"response\": \"connect\","));
  printKey();
  Serial.print(F("}\n"));

  SerialAT.println("AT+CMGS=" + sender);
  delay(100);
  SerialAT.print("{");
  SerialAT.print("\"response\": \"connect\",");
  SerialAT.print("\"key\": \"");
  SerialAT.print(chipID);
  SerialAT.print("\"}\032");
}

void sendDisconnect()
{
  Serial.print(F("Sender response: "));
  Serial.print(F("{"));
  Serial.print(F("\"response\": \"disconnect\","));
  printKey();
  Serial.print(F("}\n"));

  SerialAT.println("AT+CMGS=" + sender);
  delay(100);
  SerialAT.print("{");
  SerialAT.print("\"response\": \"disconnect\",");
  SerialAT.print("\"key\": \"");
  SerialAT.print(chipID);
  SerialAT.print("\"}\032");
}

bool validateMsg() {
  Serial.println("Validating: "+chipID);
  Serial.println(msg);
  String secret = inputString.substring(2, 20);
  Serial.println(secret);
  if (secret == chipID) {
    return true;
  }
  return false;
}

void relayOn()
{
  relay = HIGH;
  digitalWrite(Relay, relay);
  relayStatus();
}

void relayOff()
{
  relay = LOW;
  digitalWrite(Relay, relay);
  relayStatus();
}

void relayStatus()
{
  SerialAT.println("AT+CMGS=" + sender);
  delay(100);
  SerialAT.print("{");
  SerialAT.print("\"response\": \"status\",");
  SerialAT.print("\"relay\": ");
  SerialAT.print(relay);
  SerialAT.print(", \"time\": \"");
  SerialAT.print(myDate);
  SerialAT.print(myTime);
  SerialAT.print("\",\"age\": ");
  SerialAT.print(myAge);
  SerialAT.print("}\032");
}

void displayInfo()
{
  Serial.print(F("Location: "));
  if (gps.location.isValid()) {
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print(gps.location.lng(), 6);
  }
  else {
    Serial.print(F("INVALID"));
  }

  Serial.print(F("  Date/Time: "));
  if (gps.date.isValid()) {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else {
    Serial.print(F("INVALID"));
  }

  Serial.print(F(" "));
  if (gps.time.isValid()) {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("."));
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.print(gps.time.centisecond());
    Serial.print(F("..."));
    Serial.print(gps.time.age());
  }
  else {
    Serial.print(F("INVALID"));
  }

  Serial.println();
}

static void printDateTime(TinyGPSDate & d, TinyGPSTime & t)
{
  myDate = "********** ";
  myAge = "* ";
  if (d.isValid()) {
    char sz[32] = { '\0'};
    sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    myDate = sz;
    sprintf(sz, "%ld", d.age());
    myAge = sz;
  }
  myTime = "********** ";
  if (t.isValid()) {
    char sz[32] = { '\0'};
    sprintf(sz, "%02d:%02d:%02d", t.hour(), t.minute(), t.second());
    myTime = sz;
  }
}


