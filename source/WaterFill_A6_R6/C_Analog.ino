//TAB - ANALOG

/***********************************************************
** Analog I/O handling for KC868-A6                       **
***********************************************************/

String AIMessage;
String AOMessage;

int* AITags[totalAI];
int* AOTags[totalAO];

void setupAnalog() {
    for (int i = 0; i < totalAI; i++) AITags[i] = &IO.AI.AI0 + i;
    for (int i = 0; i < totalAO; i++) AOTags[i] = &IO.AO.AO0 + i;
    // Optional: Configure AO pins for PWM if needed
    // if (enabled.AO) {
    //     pinMode(AO_Pin[0], OUTPUT);
    //     pinMode(AO_Pin[1], OUTPUT);
    // }
}

void loopAnalog() {
    AIMessage = "";
    AOMessage = "";
    int columnWidth = 16;

    if (enabled.AI) {
        for (int i = 0; i < totalAI; i++) {
            int RawAI = analogRead(AI_Pin[i]);
            *AITags[i] = map(RawAI, 0, 4095, 0, 100); // Scale 0-4095 (ADC) to 0-100
            if (enabled.debug.AI) {
                String varName = alias.AI.name[i]; // Use alias name
                AIMessage += rightJustify(varName, columnWidth) + " (AI " + String(i) + ", Pin " + String(AI_Pin[i]) + ") = " + rightJustify(String(*AITags[i]), 4) + "\n";
            }
        }
    }

    if (enabled.AO) {
        for (int i = 0; i < totalAO; i++) {
            int scaledAO = map(*AOTags[i], 0, 100, 0, 255); // Scale 0-100 to 0-255 (PWM)
            analogWrite(AO_Pin[i], scaledAO);
            if (enabled.debug.AO) {
                String varName = alias.AO.name[i]; // Use alias name
                AOMessage += rightJustify(varName, columnWidth) + " (AO " + String(i) + ", Pin " + String(AO_Pin[i]) + ") = " + rightJustify(String(*AOTags[i]), 4) + "\n";
            }
        }
    }
    delay(10);
}

