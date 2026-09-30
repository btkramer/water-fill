/*********************************************************************
** KC868-A6 Smart Controller                                        **
** https://www.kincony.com                                          **
** Main tab: Core setup, loop, and global configuration             **
** User adjustable tag names and settings:                          **
** Enable/Disable features in 'struct Enable', Main Tab             **
** Define Pin #s in 'setupMap', Map Tab                             **
** Rename user tag names in 'struct Alias', Map Tab                 **
** Default Times in 'struct TimeData, AutoStart', Time Tab          **
** Define tags to save/load to EEPROM, EEPROM Tab                   **
**********************************************************************/

// Configuration to enable/disable hardware features and debugging
struct Enabled {
    bool RTC = true;         // Enable Real-Time Clock
    bool EEPROM = true;      // Enable EEPROM storage
    bool DI = true;          // Enable Discrete Inputs
    bool DO = true;          // Enable Discrete Outputs
    bool AI = false;         // Enable Analog Inputs
    bool AO = false;         // Enable Analog Outputs
    bool WiFi = true;        // Enable WiFi connectivity
    bool Serial2 = false;    // Enable Serial2
    bool WebSerial = true;   // Enable WebSerial interface
    bool WebServer = true;   // Enable WebServer
    bool WebTime = false;    // Enable NTP time sync
    bool OTA = true;         // Enable Over the Air updates

    bool CPAP = true;        // Enable CPAP device
    bool BrianDP = true;     // Enable Brian's Dock Pro device
    bool DinaDP = true;      // Enable Dina's Dock Pro device
    bool Ozone = true;       // Enable Ozone device
    bool* deviceEnableStates[4] = {&CPAP, &BrianDP, &DinaDP, &Ozone};

    struct Debug {
        bool TimeDate = true; // Enable Time/Date debug output
        bool DI = true;       // Enable Discrete Input debug output
        bool DO = true;       // Enable Discrete Output debug output
        bool AI = false;      // Enable Analog Input debug output
        bool AO = false;      // Enable Analog Output debug output
        bool Control = true;  // Enable Control debug output
    } debug;
} enabled;

// Load Libraries
#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>
#include <elapsedMillis.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <RTClib.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WebSerial.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h> //Default password is 'admin'


// General-purpose variables
struct Variables {
    bool mode = true; // Auto mode (true) vs. manual (false)
} var;


void setup() {
    Serial.begin(921600);
    delay(1000);
    
    SerialPrint("");
    SerialPrint("");
    SerialPrint("Setup intializing....");

    setupMap();               
    if (enabled.WiFi) setupWiFi(); 
    if (enabled.EEPROM) setupEEPROM(); 
    if (enabled.RTC || enabled.WebTime) setupTime();
    if (enabled.WebServer) {       
        setupmDNS();               
        setupWebServer();           
    }
    if (enabled.WebSerial) setupWebSerial(); 
    if (enabled.OTA) setupOTA();    
    setupDiscrete();                
    setupAnalog();                  

    SerialPrint("Setup complete");
    SerialPrint("");
    SerialPrint("");
}


// Timers for periodic tasks
struct Timers {
    struct Loop {
        elapsedMillis RTC = 0;      // RTC update timer
        elapsedMillis Discrete = 0; // Discrete I/O timer
        elapsedMillis Analog = 0;   // Analog I/O timer
        elapsedMillis WebSerial = 0;// WebSerial timer
        elapsedMillis Control = 0;  // Control logic timer
        elapsedMillis OTA = 0;  // Over the Air update timer
        elapsedMillis Debug = 0;    // Debug output timer

        struct Intervals {
            const unsigned long RTC = 230;
            const unsigned long Discrete = 20;
            const unsigned long Analog = 170;
            const unsigned long WebSerial = 310;
            const unsigned long Control = 50;
            const unsigned long OTA = 1050;
            const unsigned long Debug = 70;
        
        } interval;
    } loop;

    struct Control {
        elapsedMillis OneSec = 0;     // 1-second timer
        elapsedMillis FiveSec = 0;    // 5-second timer
        elapsedMillis TenSec = 11000; // 10-second timer (offset to avoid boot trigger)
        elapsedMillis Ozone = 61000;  // Ozone run timer
        elapsedMillis PumpRun = 0;// Pump run timer
        elapsedMillis Debug = -10000; // Timer for debug output, offset to trigger 5 seconds after boot at default)
    } control;
} timer;


void loop() {
    if ((enabled.RTC || enabled.WebTime) && timer.loop.RTC >= timer.loop.interval.RTC) {
        timer.loop.RTC = 0;
        loopTime();
    }
    if (timer.loop.Discrete >= timer.loop.interval.Discrete) {
        timer.loop.Discrete = 0;
        loopDiscrete();
    }
    if (timer.loop.Analog >= timer.loop.interval.Analog) {
        timer.loop.Analog = 0;
        loopAnalog();
    }
    if (enabled.WebSerial && timer.loop.WebSerial >= timer.loop.interval.WebSerial) {
        timer.loop.WebSerial = 0;
        loopWebSerial();
    }
    if (timer.loop.Control >= timer.loop.interval.Control) {
        timer.loop.Control = 0;
        loopControl();
    }
    if (timer.loop.Debug >= timer.loop.interval.Debug) {
        timer.loop.Debug = 0;
        loopDebug();
    }
    if (enabled.OTA && timer.loop.OTA >= timer.loop.interval.OTA) {
        timer.loop.OTA = 0;
        loopOTA();
    }

}

