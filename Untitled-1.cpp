
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD address: usually 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// TWO INDEPENDENT SERVO GATES
Servo entryGate;
Servo exitGate;

// PIN SETTINGS
const byte ENTRY_IR = 22;
const byte EXIT_IR = 23;

const byte ENTRY_SERVO = 6;
const byte EXIT_SERVO = 10;

const byte RED_LED = 7;
const byte BUZZER = 8;
const byte EXIT_BUTTON = 9;

// PARKING SETTINGS
const byte MAX_SLOTS = 6;
byte parkedCars = 0;

// Most IR modules give LOW when detecting.
// Change to HIGH if your sensor works oppositely.
const byte IR_ACTIVE = LOW;

// SERVO ANGLES
const byte GATE_CLOSED = 0;
const byte GATE_OPEN = 90;

// TIMINGS
const unsigned long GATE_DURATION = 3000;
const unsigned long AUTH_DURATION = 10000;
const unsigned long ALARM_DURATION = 3000;
const unsigned long BLINK_DURATION = 400;

// PARKING ENTRY TIMES
unsigned long parkingStart[MAX_SLOTS];

// INDEPENDENT GATE TIMERS
bool entryGateOpen = false;
bool exitGateOpen = false;

unsigned long entryGateTime = 0;
unsigned long exitGateTime = 0;

// OTHER TIMERS
bool authorized = false;
unsigned long authTime = 0;

bool alarmOn = false;
unsigned long alarmTime = 0;

unsigned long ledTime = 0;
unsigned long lcdTime = 0;

bool ledState = false;

// SENSOR DEBOUNCING
struct Sensor {
  byte pin;
  byte lastReading;
  byte stableReading;
  unsigned long changedAt;
};

Sensor entrySensor;
Sensor exitSensor;
Sensor buttonSensor;

// INITIALIZE SENSOR
void initSensor(Sensor &s, byte pin, byte mode) {
  s.pin = pin;
  pinMode(pin, mode);

  s.lastReading = digitalRead(pin);
  s.stableReading = s.lastReading;
  s.changedAt = millis();
}

// DETECT A NEW OBJECT
bool detectObject(Sensor &s, byte activeState) {

  byte reading = digitalRead(s.pin);

  if (reading != s.lastReading) {
    s.lastReading = reading;
    s.changedAt = millis();
  }

  if (millis() - s.changedAt >= 60) {

    if (s.stableReading != s.lastReading) {

      s.stableReading = s.lastReading;

      if (s.stableReading == activeState) {
        return true;
      }
    }
  }

  return false;
}

// OPEN ENTRY GATE
void openEntryGateNow() {
  entryGate.write(GATE_OPEN);
  entryGateOpen = true;
  entryGateTime = millis();
}

// OPEN EXIT GATE
void openExitGateNow() {
  exitGate.write(GATE_OPEN);
  exitGateOpen = true;
  exitGateTime = millis();
}

// CLOSE GATES INDEPENDENTLY
void manageGates() {

  if (entryGateOpen &&
      millis() - entryGateTime >= GATE_DURATION) {

    entryGate.write(GATE_CLOSED);
    entryGateOpen = false;
  }

  if (exitGateOpen &&
      millis() - exitGateTime >= GATE_DURATION) {

    exitGate.write(GATE_CLOSED);
    exitGateOpen = false;
  }
}

// START ALARM
void startAlarm() {
  alarmOn = true;
  alarmTime = millis();
  digitalWrite(BUZZER, HIGH);
}

// MANAGE ALARM
void manageAlarm() {

  if (alarmOn &&
      millis() - alarmTime >= ALARM_DURATION) {

    digitalWrite(BUZZER, LOW);
    alarmOn = false;
  }
}

// RED LED WHEN PARKING IS FULL
void manageFullLED() {

  if (parkedCars >= MAX_SLOTS) {

    if (millis() - ledTime >= BLINK_DURATION) {
      ledTime = millis();
      ledState = !ledState;
      digitalWrite(RED_LED, ledState);
    }

  } else {
    ledState = false;
    digitalWrite(RED_LED, LOW);
  }
}

// DISPLAY PARKING TIME
void displayParkingTime() {

  if (parkedCars == 0) {
    lcd.setCursor(0, 1);
    lcd.print("NO CARS PARKED ");
    return;
  }

  // Display oldest parked vehicle
  unsigned long elapsed =
      (millis() - parkingStart[0]) / 1000;

  unsigned long hours = elapsed / 3600;
  unsigned long minutes = (elapsed / 60) % 60;
  unsigned long seconds = elapsed % 60;

  char timeText[17];

  snprintf(timeText, sizeof(timeText),
           "TIME %02lu:%02lu:%02lu",
           hours, minutes, seconds);

  lcd.setCursor(0, 1);
  lcd.print(timeText);
}

// UPDATE LCD
void updateLCD() {

  lcd.setCursor(0, 0);

  lcd.print("FREE: ");
  lcd.print(MAX_SLOTS - parkedCars);
  lcd.print("/");
  lcd.print(MAX_SLOTS);
  lcd.print("        ");

  displayParkingTime();
}

// VEHICLE ENTERS
void vehicleEntry() {

  if (parkedCars < MAX_SLOTS) {

    parkingStart[parkedCars] = millis();
    parkedCars++;

    openEntryGateNow();

    Serial.println("VEHICLE ENTERED");

  } else {

    Serial.println("PARKING FULL");

  }
}

// VEHICLE EXITS
void vehicleExit() {

  if (authorized && parkedCars > 0) {

    unsigned long duration =
        (millis() - parkingStart[0]) / 1000;

    Serial.print("Vehicle parking time: ");
    Serial.print(duration);
    Serial.println(" seconds");

    // Remove oldest vehicle
    for (byte i = 0; i < parkedCars - 1; i++) {
      parkingStart[i] = parkingStart[i + 1];
    }

    parkedCars--;

    authorized = false;

    openExitGateNow();

    Serial.println("AUTHORIZED EXIT");

  } else {

    startAlarm();

    Serial.println("UNAUTHORIZED EXIT!");
  }
}

// SETUP
void setup() {

  Serial.begin(9600);

  initSensor(entrySensor, ENTRY_IR, INPUT);
  initSensor(exitSensor, EXIT_IR, INPUT);

  initSensor(buttonSensor, EXIT_BUTTON, INPUT_PULLUP);

  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  entryGate.attach(ENTRY_SERVO);
  exitGate.attach(EXIT_SERVO);

  entryGate.write(GATE_CLOSED);
  exitGate.write(GATE_CLOSED);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("SMART PARKING");

  lcd.setCursor(0, 1);
  lcd.print("SYSTEM READY");

  delay(2000);
  lcd.clear();

  updateLCD();
}

// MAIN LOOP
void loop() {

  // 1. EXIT AUTHORIZATION BUTTON
  if (detectObject(buttonSensor, LOW)) {

    authorized = true;
    authTime = millis();

    Serial.println("EXIT AUTHORIZED");
  }

  // Authorization expires after 10 seconds
  if (authorized &&
      millis() - authTime >= AUTH_DURATION) {

    authorized = false;
  }

  // 2. VEHICLE ENTRY
  if (detectObject(entrySensor, IR_ACTIVE)) {
    vehicleEntry();
  }

  // 3. VEHICLE EXIT
  if (detectObject(exitSensor, IR_ACTIVE)) {
    vehicleExit();
  }

  // 4. CONTROL TWO GATES
  manageGates();

  // 5. BLINK RED LED WHEN FULL
  manageFullLED();

  // 6. ANTI-THEFT BUZZER
  manageAlarm();

  // 7. LCD WARNING AND INFORMATION
  if (alarmOn) {

    lcd.setCursor(0, 0);
    lcd.print("UNAUTHORIZED!   ");

    lcd.setCursor(0, 1);
    lcd.print("THEFT WARNING   ");

  } else if (millis() - lcdTime >= 500) {

    lcdTime = millis();
    updateLCD();
  }
}
