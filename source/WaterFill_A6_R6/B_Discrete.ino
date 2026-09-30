// TAB - DISCRETE

/***********************************************************
** Discrete I/O handling for KC868-A6                     **
** LOW (0) = ON, HIGH (1) = OFF                           **
***********************************************************/

String DIMessage;
String MessageDI;
String DOMessage;
String MessageDO;

const unsigned long debounceDelay = 100; // Debounce delay in milliseconds

// Global arrays for tag references
bool* DITags[totalDI];
bool* DOTags[totalDO];

int lastDIState[totalDI] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
bool lastDOState[totalDO] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
elapsedMillis debounceTimers[totalDI];

void setupDiscrete() {
    for (int i = 0; i < totalDI; i++) DITags[i] = &IO.DI.DI0 + i;
    for (int i = 0; i < totalDO; i++) DOTags[i] = &IO.DO.DO0 + i;

    if (enabled.DI) {
        for (int i = 0; i < totalDI; i++) {
            PFC_DI.pinMode(i, INPUT);
        }
        SerialPrint("Init DI...");
        if (PFC_DI.begin()) {
            SerialPrint("OK");
        } else {
            SerialPrint("KO");
        }
    }

    if (enabled.DO) {
        for (int i = 0; i < totalDO; i++) {
            PFC_DO.pinMode(i, OUTPUT);
            *DOTags[i] = HIGH; // Set outputs OFF
            lastDOState[i] = HIGH; // Sync lastDOState with initial state
            PFC_DO.digitalWrite(i, HIGH); // Explicitly set hardware OFF
        }
        SerialPrint("Init DO...");
        if (PFC_DO.begin()) {
            SerialPrint("OK");
        } else {
            SerialPrint("KO");
        }
        SerialPrint("");
    }

    delay(10);
}

void loopDiscrete() {
    DIMessage = "";
    DOMessage = "";
    int columnWidth = 20;

    if (enabled.DI) {
        for (int inPin = 0; inPin < totalDI; inPin++) {
            bool reading = PFC_DI.digitalRead(inPin);
            if (reading != lastDIState[inPin]) {
                debounceTimers[inPin] = 0;
            }
            if (debounceTimers[inPin] > debounceDelay && reading != *DITags[inPin]) {
                *DITags[inPin] = reading;
                String messageDI = String(alias.DI.name[inPin]) + " is now " + (reading ? "OFF" : "ON");
                SerialPrint(messageDI);
            }
            lastDIState[inPin] = reading;
            if (enabled.debug.DI) {
                String diName = String(alias.DI.name[inPin]);
                String diValue = (*DITags[inPin] == HIGH ? "OFF" : "ON");
                DIMessage += rightJustify(diName, columnWidth) + " (DI " + String(inPin) + ") = " + rightJustify(diValue, 3) + "\n";
            }
        }
    }

    if (enabled.DO) {
        for (int outPin = 0; outPin < totalDO; outPin++) {
            bool currentOutputState = *DOTags[outPin];
            bool previousOutputState = lastDOState[outPin];
            if (currentOutputState != previousOutputState) {
                PFC_DO.digitalWrite(outPin, currentOutputState ? HIGH : LOW);
                lastDOState[outPin] = currentOutputState;
                String outputStateMessage = String(alias.DO.name[outPin]) + " is now " + (currentOutputState ? "OFF" : "ON");
                SerialPrint(outputStateMessage);
            }
            if (enabled.debug.DO) {
                String doName = String(alias.DO.name[outPin]);
                String doValue = (currentOutputState == HIGH ? "OFF" : "ON");
                DOMessage += rightJustify(doName, columnWidth) + " (DO " + String(outPin) + ") = " + rightJustify(doValue, 3) + "\n";
            }
        }
    }
}