//TAB - CONTROL

/***********************************************************
** Control logic for KC868-A6 water filling operations    **
** Controls pump and solenoids in Auto/Manual modes       **
** State machine: FillStep drives Auto mode sequence      **
***********************************************************/

// === STATE ENUM ===
enum FillSteps {
    IDLE = 0,
    FILL_CPAP = 1,
    CHECK_BRIAN = 2,
    FILL_BRIAN = 3,
    CHECK_DINA = 4,
    FILL_DINA = 5,
    RUN_OZONE = 6
};

// Global state
FillSteps FillStep = IDLE;

void loopControl() {
    if (var.mode) { // Auto = true, Manual = false
        loopAutoMode();
    }
}

// Helper function to stop all devices
void Stop() {
    alias.DO.Pump_R0 = true;     // OFF (HIGH = 1)
    alias.DO.CPAP_R1 = true;
    alias.DO.BrianDP_R2 = true;
    alias.DO.DinaDP_R3 = true;
    alias.DO.Ozone_R4 = true;
}

// Helper function to run pump with timing
void PumpRun() {
    unsigned long cycleTime = (timeData.setpoint.MaxPumpRun + timeData.setpoint.PumpOff) * 1000UL;
    unsigned long onTime    = timeData.setpoint.MaxPumpRun * 1000UL;

    if (timer.control.PumpRun < onTime) {
        alias.DO.Pump_R0 = false; // ON
        SerialPrint("Pump ON: " + String(timer.control.PumpRun / 1000) + "s");
    } 
    else if (timer.control.PumpRun < cycleTime) {
        alias.DO.Pump_R0 = true;  // OFF (cool-down)
        SerialPrint("Pump OFF (cool-down): " + String((timer.control.PumpRun - onTime) / 1000) + "s");
    } 
    else {
        alias.DO.Pump_R0 = true;  // Ensure OFF
        SerialPrint("Pump cycle complete. Resetting timer.");
        timer.control.PumpRun = 0; // Reset for next cycle
    }
}

void CPAP_Fill() {
    PumpRun();
    alias.DO.CPAP_R1 = false;     // ON
    alias.DO.BrianDP_R2 = true;   // OFF
    alias.DO.DinaDP_R3 = true;    // OFF
    alias.DO.Ozone_R4 = true;     // OFF
}

void BriansDP_Fill() {
    PumpRun();
    alias.DO.CPAP_R1 = true;     // OFF
    alias.DO.BrianDP_R2 = false;  // ON
    alias.DO.DinaDP_R3 = true;    // OFF
    alias.DO.Ozone_R4 = true;     // OFF
}

void DinasDP_Fill() {
    PumpRun();
    alias.DO.CPAP_R1 = true;     // OFF
    alias.DO.BrianDP_R2 = true;  // OFF
    alias.DO.DinaDP_R3 = false;   // ON
    alias.DO.Ozone_R4 = true;     // OFF
}

void Ozone_Run() {
    alias.DO.Pump_R0 = true;      // OFF
    alias.DO.CPAP_R1 = true;      // OFF
    alias.DO.BrianDP_R2 = true;   // OFF
    alias.DO.DinaDP_R3 = true;    // OFF
    alias.DO.Ozone_R4 = false;    // ON
}

// Timeout protection
static elapsedMillis stepTimer = 0;
const unsigned long STEP_TIMEOUT = 30000;  // 30 sec max per step

void loopAutoMode() {
    // === TIMEOUT SAFETY ===
    if (FillStep != IDLE && stepTimer > STEP_TIMEOUT) {
        SerialPrint(F("STEP TIMEOUT: FillStep="));
        SerialPrint(static_cast<int>(FillStep));
        SerialPrint(F(" — resetting to IDLE"));
        Stop();
        FillStep = IDLE;
        stepTimer = 0;
        return;
    }

    // === STATE MACHINE ===
    switch (FillStep) {

        case IDLE: // Idle / Start
            // === PRIORITY: CPAP → Brian → Dina ===
            if (enabled.CPAP && !alias.DI.CPAP_LSL_DI3) {
                SerialPrint(F("Starting CPAP fill"));
                timer.control.PumpRun = 0;
                FillStep = FILL_CPAP;
                stepTimer = 0;
            }
            else if (enabled.BrianDP && !alias.DI.BriansDP_LSL_DI4) {
                SerialPrint(F("Starting Brian's Dock Pro fill"));
                timer.control.PumpRun = 0;
                FillStep = FILL_BRIAN;
                stepTimer = 0;
            }
            else if (enabled.DinaDP && !alias.DI.DinasDP_LSL_DI5) {
                SerialPrint(F("Starting Dina's Dock Pro fill"));
                timer.control.PumpRun = 0;
                FillStep = FILL_DINA;
                stepTimer = 0;
            }
            // === OZONE: Independent start ===
            else if (enabled.Ozone && !alias.DI.Button_SW1_DI1) {
                timer.control.Ozone = 0;
                SerialPrint(F("Ozone button pressed. Starting "));
                SerialPrint(timeData.setpoint.OzoneRun);
                SerialPrint(F("s run."));
                FillStep = RUN_OZONE;
                stepTimer = 0;
            }
            break;

        case FILL_CPAP:
            if (enabled.CPAP && !alias.DI.CPAP_LSL_DI3) {
                CPAP_Fill();
            } else {
                SerialPrint(F("CPAP fill complete"));
                Stop();
                SerialPrint(F("Checking Brian's Dock Pro level"));
                FillStep = CHECK_BRIAN;
                stepTimer = 0;
            }
            break;

        case CHECK_BRIAN:
            if (enabled.BrianDP && !alias.DI.BriansDP_LSL_DI4) {
                SerialPrint(F("Filling Brian's Dock Pro"));
                timer.control.PumpRun = 0;
                FillStep = FILL_BRIAN;
                stepTimer = 0;
            } else {
                SerialPrint(F("Brian's Dock Pro does not need filling"));
                Stop();
                SerialPrint(F("Checking Dina's Dock Pro level"));
                FillStep = CHECK_DINA;
                stepTimer = 0;
            }
            break;

        case FILL_BRIAN:
            if (enabled.BrianDP && !alias.DI.BriansDP_LSL_DI4) {
                BriansDP_Fill();
            } else {
                SerialPrint(F("Brian's Dock Pro fill complete"));
                Stop();
                SerialPrint(F("Checking Dina's Dock Pro level"));
                FillStep = CHECK_DINA;
                stepTimer = 0;
            }
            break;

        case CHECK_DINA:
            if (enabled.DinaDP && !alias.DI.DinasDP_LSL_DI5) {
                SerialPrint(F("Filling Dina's Dock Pro"));
                timer.control.PumpRun = 0;
                FillStep = FILL_DINA;
                stepTimer = 0;
            } else {
                SerialPrint(F("Dina's Dock Pro does not need filling"));
                Stop();
                FillStep = IDLE;
                stepTimer = 0;
            }
            break;

        case FILL_DINA:
            if (enabled.DinaDP && !alias.DI.DinasDP_LSL_DI5) {
                DinasDP_Fill();
            } else {
                SerialPrint(F("Dina's Dock Pro fill complete"));
                Stop();
                SerialPrint(F("Finished filling"));
                FillStep = IDLE;
                stepTimer = 0;
            }
            break;

        case RUN_OZONE:
            if ((timer.control.Ozone / 1000) < timeData.setpoint.OzoneRun) {
                Ozone_Run();
            } else {
                SerialPrint(F("Ozone complete"));
                Stop();
                FillStep = IDLE;
                stepTimer = 0;
            }
            break;
    }
}