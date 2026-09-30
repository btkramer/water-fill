// TAB - MAP
/***********************************************************
** Hardware pin mapping and I2C configuration for KC868-A6**
***********************************************************/

// Define I2C pins for communication
#define SDA_PIN 4    // I2C SDA pin
#define SCL_PIN 15   // I2C SCL pin

// PCF8574 IO Expander (I2C addresses for KC868-A6)
PCF8574 PFC_DI(0x22, SDA_PIN, SCL_PIN); // Discrete Inputs expander
PCF8574 PFC_DO(0x24, SDA_PIN, SCL_PIN); // Discrete Outputs expander

// Define number of IO pins
const int totalDI = 6; // Number of discrete inputs
const int totalDO = 6; // Number of discrete outputs
const int totalAI = 4; // Number of analog inputs
const int totalAO = 2; // Number of analog outputs

// Analog pin arrays
int AI_Pin[totalAI]; // Analog input pins (0-4096, 0-5V)
int AO_Pin[totalAO]; // Analog output pins (0-255, 0-10V)

// Hardware I/O structure (raw tags)
struct InputOutputs {
    struct DiscreteInputs {
        bool DI0 = true; // Discrete Input 0
        bool DI1 = true; // Discrete Input 1
        bool DI2 = true; // Discrete Input 2
        bool DI3 = true; // Discrete Input 3
        bool DI4 = true; // Discrete Input 4
        bool DI5 = true; // Discrete Input 5
    } DI;

    struct DiscreteOutputs {
        bool DO0 = true; // Discrete Output 0
        bool DO1 = true; // Discrete Output 1
        bool DO2 = true; // Discrete Output 2
        bool DO3 = true; // Discrete Output 3
        bool DO4 = true; // Discrete Output 4
        bool DO5 = true; // Discrete Output 5
    } DO;

    struct AnalogInputs {
        int AI0 = 0; // Analog Input 0
        int AI1 = 0; // Analog Input 1
        int AI2 = 0; // Analog Input 2
        int AI3 = 0; // Analog Input 3
    } AI;

    struct AnalogOutputs {
        int AO0 = 0; // Analog Output 0
        int AO1 = 0; // Analog Output 1
    } AO;
} IO;

// User-friendly aliases for hardware tags and device metadata, Rename as needed
struct Alias {
    struct DI {
        bool& Input_DI0 = IO.DI.DI0;       // Bad, Input 0
        bool& Button_SW1_DI1 = IO.DI.DI1; // Button, Input 1
        bool& Tank_LSL_DI2 = IO.DI.DI2;   // Water Tank Level Switch Low, Input 2
        bool& CPAP_LSL_DI3 = IO.DI.DI3;   // CPAP Level Switch Low, Input 3
        bool& BriansDP_LSL_DI4 = IO.DI.DI4; // Brian's Dock Pro Level Switch Low, Input 4
        bool& DinasDP_LSL_DI5 = IO.DI.DI5;  // Dina's Dock Pro Level Switch Low, Input 5
        const char* name[totalDI] = { "Bad DI0", "Button", "Tank LSL", "CPAP LSL", "Brian's DP LSL", "Dina's DP LSL" };
    } DI;

    struct DO {
        bool& Pump_R0 = IO.DO.DO0;      // Pump, Output 0
        bool& CPAP_R1 = IO.DO.DO1;      // CPAP Solenoid, Output 1
        bool& BrianDP_R2 = IO.DO.DO2;   // Brian's Dock Pro Solenoid, Output 2
        bool& DinaDP_R3 = IO.DO.DO3;    // Dina's Dock Pro Solenoid, Output 3
        bool& Ozone_R4 = IO.DO.DO4;     // Ozone, Output 4
        bool& Output_R5 = IO.DO.DO5;    // Spare, Output 5
        const char* name[totalDO] = { "Pump", "CPAP Sol", "Brian's DP Sol", "Dina's DP Sol", "Ozone", "Spare DO5" };
    } DO;

    struct AI {
        int& AI_Spare_0 = IO.AI.AI0; // Spare Analog Input 0
        int& AI_Spare_1 = IO.AI.AI1; // Spare Analog Input 1
        int& AI_Spare_2 = IO.AI.AI2; // Spare Analog Input 2
        int& AI_Spare_3 = IO.AI.AI3; // Spare Analog Input 3
        const char* name[totalAI] = { "Spare AI0", "Spare AI1", "Spare AI2", "Spare AI3" };
    } AI;

    struct AO {
        int& AO_Spare_0 = IO.AO.AO0; // Spare Analog Output 0
        int& AO_Spare_1 = IO.AO.AO1; // Spare Analog Output 1
        const char* name[totalAO] = { "Spare AO0", "Spare AO1" };
    } AO;

    // Device metadata for web interface and control logic
    const char* deviceTags[4] = {"CPAP", "BrianDP", "DinaDP", "Ozone"}; // Tags for device identification
    const char* deviceNames[4] = {"CPAP", "Brian's Dock Pro", "Dina's Dock Pro", "Ozone"}; // Display names
} alias;

void setupMap() {
    Wire.setPins(SDA_PIN, SCL_PIN); // Configure I2C pins
    Wire.begin();
    Wire.setClock(400000); // Set I2C clock speed to 400kHz

    // Check I2C device presence (PCF8574 expanders)
    bool i2cError = false;
    Wire.beginTransmission(0x22); // Check DI expander
    if (Wire.endTransmission() != 0) {
        SerialPrint("I2C DI expander (0x22) not detected!");
        i2cError = true;
    }
    Wire.beginTransmission(0x24); // Check DO expander
    if (Wire.endTransmission() != 0) {
        SerialPrint("I2C DO expander (0x24) not detected!");
        i2cError = true;
    }
    if (!i2cError) {
        SerialPrint("I2C setup successful, DI and DO expanders detected.");
    }

    // Initialize Analog Input Pins
    AI_Pin[0] = 36; // GPIO36
    AI_Pin[1] = 39; // GPIO39
    AI_Pin[2] = 34; // GPIO34
    AI_Pin[3] = 35; // GPIO35

    // Initialize Analog Output Pins (PWM-capable)
    AO_Pin[0] = 26; // GPIO26
    AO_Pin[1] = 25; // GPIO25

    // Define Serial2 pins
    if (enabled.Serial2) {
        #define RX2_PIN 12 // RX pin for Serial2
        #define TX2_PIN 13 // TX pin for Serial2
        Serial2.begin(921600, SERIAL_8N1, RX2_PIN, TX2_PIN);
        Serial2.println("Serial2 started");
    }
}

/*
//Select Sensor name, delete rest.
#define DS18B20/DHT11/DHT21/LED -1: 32
#define DS18B20/DHT11/DHT21/LED -2: 33

DS18B20 ds1(32);  //channel-1-DS18b20, IO-1
DS18B20 ds2(33);  //channel-2-DS18b20, IO-2

SerialPrint("T1: %.2f\n",ds1.getTempC());
SerialPrint("T2: %.2f\n",ds2.getTempC());
delay(100);

//IO-1(32) & IO-2(33) can also be used for PWM
//pulseIn(pwmPin, HIGH): Measures the duration the pin stays HIGH in microseconds.
//pulseIn(pwmPin, LOW): Measures the duration the pin stays LOW.
//Adding both high and low durations gives the total period, from which you can calculate the frequency.
//The duty cycle is computed by dividing the high duration by the total period and multiplying by 100 to get a percentage.

int pwmPin = 32; // Assume PWM input is connected to GPIO 32

void setupPWM() {

  pinMode(pwmPin, INPUT);

}

void loopPWM() {
  // Measure the high time (in microseconds)
  unsigned long highDuration = pulseIn(pwmPin, HIGH);
  // Measure the low time (in microseconds)
  unsigned long lowDuration = pulseIn(pwmPin, LOW);
 
  // Calculate the total period
  unsigned long period = highDuration + lowDuration;

  // Calculate the duty cycle (percentage)
  float dutyCycle = (float)highDuration / period * 100;

  // Calculate the frequency (Hz)
  float frequency = 1000000.0 / period;

  // Print the results
  Serial.print("Frequency: ");
  Serial.print(frequency);
  Serial.print(" Hz, Duty Cycle: ");
  Serial.print(dutyCycle);
  Serial.println(" %");

  delay(1000); // Print every second
}

#define RS485_RXD 14
#define RS485_TXD 27

*************************RS485初始输出******************************
Serial2.begin(115200,SERIAL_8N1,14,27);  //  IO14   485RX   IO27 485TX
Serial2.printf("485 send is OK!\n"); // Enter your print string

//SPI_Bus: (For LoRA/nRF24L01)
#define CS 5
#define MOSI 23
#define MISO 19
#define SCK 18

//LoRA sx1278:
RST:21
DIO0:2

//nRF24L01:
CE: 22

//RS232:
#define RS232_TXD 17
#define RS232_RXD 16

//Extend serial port on PCB:
TXD(define by yourself):12
RXD (define by yourself) :13
*/

