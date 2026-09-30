//TAB - DEBUG

/***********************************************************
** Debug output handling for KC868-A6                     **
** Prints state info at intervals set by enabled.debug.Time**
***********************************************************/

#define F(x) (const __FlashStringHelper*)x


void loopDebug() {
    if (timer.control.Debug >= (timeData.setpoint.Debug * 1000)) {
        TimedSerialPrint();
        timer.control.Debug = 0; // Reset the timer after printing
    }
}


// === UNIVERSAL SerialPrint ===
// Core function: takes String by reference (no copy)
void SerialPrint(const String& message) {
    Serial.println(message);
    if (enabled.Serial2) Serial2.println(message);
    if (enabled.WebSerial) {
        WebSerial.println(message);
        WebSerial.flush();
    }
}

// Overloads: convert and forward (no recursion)
void SerialPrint(const char* message) {
    if (message) SerialPrint(String(message));  // Safe: only one conversion
    else SerialPrint(F("(null)"));
}

void SerialPrint(char* message) { SerialPrint(static_cast<const char*>(message)); }

void SerialPrint(int value)           { SerialPrint(String(value)); }
void SerialPrint(unsigned int value)  { SerialPrint(String(value)); }
void SerialPrint(long value)          { SerialPrint(String(value)); }
void SerialPrint(unsigned long value) { SerialPrint(String(value)); }
void SerialPrint(float value)         { SerialPrint(String(value, 2)); }
void SerialPrint(double value)        { SerialPrint(String(value, 2)); }

// Base support (HEX, BIN, etc.)
void SerialPrint(int value, int base) {
    char buf[33];
    char* str = (char*)buf;
    if (base == HEX) {
        snprintf(str, sizeof(buf), "0x%X", value);
    } else if (base == BIN) {
        str[0] = '0'; str[1] = 'b'; itoa(value, str + 2, 2);
    } else {
        itoa(value, str, base);
    }
    SerialPrint(String(str));
}


// Helper function to add padding for right justification
String rightJustify(String text, int width) {
  while (text.length() < width) {
    text = " " + text;  // Add spaces to the left
  }
  return text;
}


void TimedSerialPrint() {
    SerialPrint("");
    SerialPrint("*******************************************");
    SerialPrint("");

    if (enabled.debug.TimeDate) {
        if (enabled.RTC) {
            printTime("RTC");
        } else if (enabled.WebTime) {
            printTime("Web");
        } else {
            SerialPrint("Time is Disabled");
        }
        SerialPrint("____________________________________________");
        SerialPrint("");
    }

    if (enabled.debug.DI) {
        if (enabled.DI) {
            SerialPrint(DIMessage);
        } else {
            SerialPrint("Discrete Inputs are Disabled");
        }
        SerialPrint("____________________________________________");
        SerialPrint("");
    }

    if (enabled.debug.DO) {
        if (enabled.DO) {
            SerialPrint(DOMessage);
        } else {
            SerialPrint("Discrete Outputs are Disabled");
        }
        SerialPrint("____________________________________________");
        SerialPrint("");
    }

    if (enabled.debug.AI) {
        if (enabled.AI) {
            SerialPrint(AIMessage);
        } else {
            SerialPrint("Analog Inputs are Disabled");
        }
        SerialPrint("____________________________________________");
        SerialPrint("");
    }

    if (enabled.debug.AO) {
        if (enabled.AO) {
            SerialPrint(AOMessage);
        } else {
            SerialPrint("Analog Outputs are Disabled");
        }
        SerialPrint("____________________________________________");
        SerialPrint("");
    }


    if (enabled.debug.Control) {
        SerialPrint("Mode: " + String(var.mode ? "Auto" : "Manual"));
        SerialPrint("Auto Start Time: " + String(timeData.autoStart.Hour < 10 ? "0" : "") + String(timeData.autoStart.Hour) + ":" +
                    String(timeData.autoStart.Minute < 10 ? "0" : "") + String(timeData.autoStart.Minute));
        SerialPrint("Max Pump Time: " + String(timeData.setpoint.MaxPumpRun));
        SerialPrint("CPAP Enabled: " + String(enabled.CPAP ? "true" : "false"));
        SerialPrint("Brian DP Enabled: " + String(enabled.BrianDP ? "true" : "false"));
        SerialPrint("Dina DP Enabled: " + String(enabled.DinaDP ? "true" : "false"));
        SerialPrint("Ozone Enabled: " + String(enabled.Ozone ? "true" : "false"));
        SerialPrint("Ozone Run Time: " + String(timeData.setpoint.OzoneRun));
    }

    if ((timer.control.Ozone / 1000) < timeData.setpoint.OzoneRun) {
        unsigned long remainingTime = timeData.setpoint.OzoneRun - (timer.control.Ozone / 1000);
        SerialPrint("");
        SerialPrint("____________________________________________");
        SerialPrint("");
        SerialPrint("Ozone Timer: " + String(remainingTime) + " seconds remaining");
    }
}

