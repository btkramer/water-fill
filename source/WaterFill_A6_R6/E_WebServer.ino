//TAB - WEBSERVER

/***********************************************************
** Web server endpoints for KC868-A6 control and monitoring**
***********************************************************/

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/time", HTTP_GET, getTimeInfo);
    server.on("/toggle", HTTP_POST, toggleDevice);
    server.on("/toggleMode", HTTP_POST, toggleMode);
    server.on("/updateMaxPumpTime", HTTP_POST, updateMaxPumpTime);
    server.on("/updateOzoneRunTime", HTTP_POST, updateOzoneRunTime);
    server.on("/toggleEnable", HTTP_POST, toggleEnable);
    server.on("/updateAutoStartTime", HTTP_POST, updateAutoStartTime);
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", getSettingsHTML());
    });
    server.on("/getDebugStates", HTTP_GET, getDebugStates);
    server.on("/toggleDebug", HTTP_POST, toggleDebug);
    server.on("/updateDebugTime", HTTP_POST, updateDebugTime);
    server.begin();
    SerialPrint("Web server started");
}

// Root endpoint: Serve main control page
void handleRoot(AsyncWebServerRequest *request) {
    request->send(200, "text/html", getHTML());
}

// Time info endpoint: Return current time in JSON
void getTimeInfo(AsyncWebServerRequest *request) {
    String timeStr = timeData.current.Formatted;
    String dateStr = timeStr.substring(0, timeStr.indexOf(" ", timeStr.indexOf(",") + 2));
    timeStr = timeStr.substring(timeStr.indexOf(" ", timeStr.indexOf(",") + 2) + 1);

    if (timeData.current.Formatted.isEmpty()) {
        timeStr = "Time not set";
        dateStr = "Date not set";
    }

    String json = "{ \"time\": \"" + timeStr + "\", \"date\": \"" + dateStr + "\", " +
                  "\"hour\": " + String(timeData.current.Hour) + ", " +
                  "\"minute\": " + String(timeData.current.Minute) + ", " +
                  "\"second\": " + String(timeData.current.Second) + ", " +
                  "\"devices\": { " +
                  "\"CPAP\": " + String(alias.DO.CPAP_R1 ? "false" : "true") + ", " +
                  "\"BrianDP\": " + String(alias.DO.BrianDP_R2 ? "false" : "true") + ", " +
                  "\"DinaDP\": " + String(alias.DO.DinaDP_R3 ? "false" : "true") + ", " +
                  "\"Ozone\": " + String(alias.DO.Ozone_R4 ? "false" : "true") + " }, " +
                  "\"levels\": { " +
                  "\"CPAP\": " + String(alias.DI.CPAP_LSL_DI3 ? "true" : "false") + ", " +
                  "\"BrianDP\": " + String(alias.DI.BriansDP_LSL_DI4 ? "true" : "false") + ", " +
                  "\"DinaDP\": " + String(alias.DI.DinasDP_LSL_DI5 ? "true" : "false") + ", " +
                  "\"Tank\": " + String(alias.DI.Tank_LSL_DI2 ? "true" : "false") + " }" +
                  "}";
    request->send(200, "application/json", json);
}


// Device toggle endpoint: Manual control of devices
void toggleDevice(AsyncWebServerRequest *request) {
    if (request->hasParam("device", true)) {
        String device = request->getParam("device", true)->value();
        int index = -1;
        for (int i = 0; i < 4; i++) {
            if (device == alias.deviceTags[i]) {
                index = i;
                break;
            }
        }
        if (index != -1 && *enabled.deviceEnableStates[index]) {
            bool* state = (index == 0) ? &alias.DO.CPAP_R1 : 
                          (index == 1) ? &alias.DO.BrianDP_R2 : 
                          (index == 2) ? &alias.DO.DinaDP_R3 : 
                                         &alias.DO.Ozone_R4;
            // Toggle the primary state
            *state = !(*state);
            // If (index 0,1 or 2), also toggle PUMP_R0
            if (index == 0|1|2) {
                delay(100);
                alias.DO.Pump_R0 = *state; // Sync R0 with R1, R2 or R3)
                SerialPrint("PUMP is " + String(alias.DO.Pump_R0 ? "OFF" : "ON"));
            }
            SerialPrint(String(alias.deviceNames[index]) + " is " + (*state ? "OFF" : "ON"));
            String response = device + ":" + (*state ? "off" : "on"); 
            request->send(200, "text/plain", response);
        } else {
            request->send(400, "text/plain", "Bad Request or Device Disabled");
        }
    } else {
        request->send(400, "text/plain", "Bad Request");
    }
}

// Mode toggle endpoint
void toggleMode(AsyncWebServerRequest *request) {
    var.mode = !var.mode;
    SerialPrint("Mode set to " + String(var.mode ? "Auto" : "Manual"));
    request->send(200, "text/plain", var.mode ? "Auto" : "Manual");
    EEPROM.write(5, var.mode ? 1 : 0);
    if (!EEPROM.commit()) {
        SerialPrint("EEPROM commit failed in toggleMode!");
    }
}

// Update MaxPumpTime endpoint
void updateMaxPumpTime(AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
        String valueStr = request->getParam("value", true)->value();
        int newValue = valueStr.toInt();
        if (newValue >= 0 && newValue <= 300) {
            timeData.setpoint.MaxPumpRun = newValue;
            SerialPrint("MaxPumpRun updated to: " + String(timeData.setpoint.MaxPumpRun));
            EEPROM.writeUShort(6, timeData.setpoint.MaxPumpRun);
            if (!EEPROM.commit()) {
                SerialPrint("EEPROM commit failed in updateMaxPumpTime!");
            }
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Invalid MaxPumpRun value");
        }
    } else {
        request->send(400, "text/plain", "No value provided");
    }
}

// Update OzoneRunTime endpoint
void updateOzoneRunTime(AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
        String valueStr = request->getParam("value", true)->value();
        int newValue = valueStr.toInt();
        if (newValue >= 0 && newValue <= 300) {
            timeData.setpoint.OzoneRun = newValue;
            SerialPrint("OzoneRun updated to: " + String(timeData.setpoint.OzoneRun));
            EEPROM.writeUShort(10, timeData.setpoint.OzoneRun);
            if (!EEPROM.commit()) {
                SerialPrint("EEPROM commit failed in updateOzoneRunTime!");
            }
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Invalid OzoneRun value");
        }
    } else {
        request->send(400, "text/plain", "No value provided");
    }
}

// Toggle device enable endpoint
void toggleEnable(AsyncWebServerRequest *request) {
    if (request->hasParam("device", true)) {
        String device = request->getParam("device", true)->value();
        int index = -1;
        for (int i = 0; i < 4; i++) {
            if (device == alias.deviceTags[i]) {
                index = i;
                break;
            }
        }
        if (index != -1) {
            *enabled.deviceEnableStates[index] = !(*enabled.deviceEnableStates[index]);
            bool* state = (index == 0) ? &alias.DO.CPAP_R1 : 
                          (index == 1) ? &alias.DO.BrianDP_R2 : 
                          (index == 2) ? &alias.DO.DinaDP_R3 : 
                                         &alias.DO.Ozone_R4;
            *state = true; // OFF if disabled or enabled (manual control takes over)
            SerialPrint(String(alias.deviceNames[index]) + " is " +
                        (*enabled.deviceEnableStates[index] ? "ENABLED" : "DISABLED") + " and " + 
                        (*state ? "OFF" : "ON"));
            EEPROM.write(1 + index, *enabled.deviceEnableStates[index] ? 1 : 0);
            if (!EEPROM.commit()) {
                SerialPrint("EEPROM commit failed in toggleEnable!");
            }
            request->send(200, "text/plain", device + ":" + (*enabled.deviceEnableStates[index] ? "enabled" : "disabled"));
        } else {
            request->send(400, "text/plain", "Invalid device");
        }
    } else {
        request->send(400, "text/plain", "Bad Request");
    }
}

// Update auto-start time endpoint
void updateAutoStartTime(AsyncWebServerRequest *request) {
    if (request->hasParam("hour", true) && request->hasParam("minute", true)) {
        int newHour = request->getParam("hour", true)->value().toInt();
        int newMinute = request->getParam("minute", true)->value().toInt();
        if (newHour >= 0 && newHour < 24 && newMinute >= 0 && newMinute < 60) {
            timeData.autoStart.Hour = newHour;
            timeData.autoStart.Minute = newMinute;
            SerialPrint("Auto Start Time updated to: " + 
                        String(timeData.autoStart.Hour < 10 ? "0" : "") + String(timeData.autoStart.Hour) + ":" +
                        String(timeData.autoStart.Minute < 10 ? "0" : "") + String(timeData.autoStart.Minute));
            EEPROM.write(8, timeData.autoStart.Hour);
            EEPROM.write(9, timeData.autoStart.Minute);
            if (!EEPROM.commit()) {
                SerialPrint("EEPROM commit failed in updateAutoStartTime!");
            }
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Invalid time");
        }
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

// Get debug states endpoint
void getDebugStates(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"TimeDate_Debug\":" + String(enabled.debug.TimeDate ? "true" : "false") + ",";
    json += "\"DI_Debug\":" + String(enabled.debug.DI ? "true" : "false") + ",";
    json += "\"DO_Debug\":" + String(enabled.debug.DO ? "true" : "false") + ",";
    json += "\"AI_Debug\":" + String(enabled.debug.AI ? "true" : "false") + ",";
    json += "\"AO_Debug\":" + String(enabled.debug.AO ? "true" : "false") + ",";
    json += "\"Control_Debug\":" + String(enabled.debug.Control ? "true" : "false") + "}";
    request->send(200, "application/json", json);
}

// Toggle debug endpoint
void toggleDebug(AsyncWebServerRequest *request) {
    if (request->hasParam("tag", true) && request->hasParam("state", true)) {
        String tag = request->getParam("tag", true)->value();
        bool state = request->getParam("state", true)->value() == "true";
        if (tag == "TimeDate_Debug") enabled.debug.TimeDate = state;
        else if (tag == "DI_Debug") enabled.debug.DI = state;
        else if (tag == "DO_Debug") enabled.debug.DO = state;
        else if (tag == "AI_Debug") enabled.debug.AI = state;
        else if (tag == "AO_Debug") enabled.debug.AO = state;
        else if (tag == "Control_Debug") enabled.debug.Control = state;
        SerialPrint("Debug " + tag + " toggled to " + (state ? "ON" : "OFF"));
        request->send(200, "text/plain", "Debug toggled");
    } else {
        request->send(400, "text/plain", "Invalid parameters");
    }
}

// Update debug time endpoint
void updateDebugTime(AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
        int newValue = request->getParam("value", true)->value().toInt();
        if (newValue >= 1 && newValue <= 60) {
            timeData.setpoint.Debug = newValue;
            SerialPrint("Debug time updated to " + String(timeData.setpoint.Debug) + " seconds");
            request->send(200, "text/plain", String(timeData.setpoint.Debug));
        } else {
            request->send(400, "text/plain", "Invalid debug time");
        }
    } else {
        request->send(400, "text/plain", "No value provided");
    }
}

