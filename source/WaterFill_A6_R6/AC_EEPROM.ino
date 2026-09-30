//TAB - EEPROM

/***********************************************************
** EEPROM setup for persistent storage on KC868-A6        **
** Layout:                                                **
** 0: Magic number (0x55)                                 **
** 1-4: Device enable states (4 bytes)                    **
** 5: Mode (1 byte)                                       **
** 6-7: MaxPumpRun (2 bytes, 0-300)                       **
** 8: AutoStartHour (1 byte, 0-23)                        **
** 9: AutoStartMinute (1 byte, 0-59)                      **
** 10-11: OzoneRun (2 bytes, 0-300)                       **
***********************************************************/

void setupEEPROM() {
    if (!enabled.EEPROM) return;

    if (!EEPROM.begin(512)) { // Check for initialization failure
        SerialPrint("EEPROM init failed!");
        return;
    }
    // Check if this is the first boot by looking for a 'magic number' in EEPROM
    if (EEPROM.read(0) != 0x55) {// 0x55 is arbitrary, used as a flag for first boot
        // First boot, write default values
        EEPROM.write(0, 0x55); // Magic number
        // Write default enable states from enabled struct (1 byte each)
        for (int i = 0; i < 4; i++) {
            EEPROM.write(1 + i, *enabled.deviceEnableStates[i] ? 1 : 0);
        }
        EEPROM.write(5, var.mode ? 1 : 0); // Mode (1 byte)
        // Write 2-byte values for MaxPumpRun and OzoneRun (0-300)
        EEPROM.write(6,  timeData.setpoint.MaxPumpRun & 0xFF);
        EEPROM.write(7, (timeData.setpoint.MaxPumpRun >> 8) & 0xFF);
        EEPROM.write(8, timeData.autoStart.Hour);     // Byte 8
        EEPROM.write(9, timeData.autoStart.Minute);   // Byte 9
        EEPROM.write(10, timeData.setpoint.OzoneRun & 0xFF);
        EEPROM.write(11, (timeData.setpoint.OzoneRun >> 8) & 0xFF);
        if (!EEPROM.commit()) {
            SerialPrint("EEPROM commit failed!");
        }
    } else {
      // Not first boot, read from EEPROM
        for (int i = 0; i < 4; i++) {
            *enabled.deviceEnableStates[i] = EEPROM.read(1 + i) == 1;
        }
        var.mode = EEPROM.read(5) == 1;
        uint16_t maxPump = EEPROM.read(6) | (EEPROM.read(7) << 8);
        timeData.setpoint.MaxPumpRun = maxPump;
        timeData.autoStart.Hour = EEPROM.read(8);
        timeData.autoStart.Minute = EEPROM.read(9);
        uint16_t ozoneRun = EEPROM.read(10) | (EEPROM.read(11) << 8);
        timeData.setpoint.OzoneRun = ozoneRun;
    }
    // Constrain values to valid ranges after reading from EEPROM
    timeData.setpoint.MaxPumpRun = constrain(timeData.setpoint.MaxPumpRun, 0, 300);
    timeData.setpoint.OzoneRun = constrain(timeData.setpoint.OzoneRun, 0, 300);
    timeData.autoStart.Hour = constrain(timeData.autoStart.Hour, 0, 23);
    timeData.autoStart.Minute = constrain(timeData.autoStart.Minute, 0, 59);
}

