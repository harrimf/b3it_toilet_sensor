#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>

typedef void (*CallbackFunc) (void);

struct scheduledEvent {
  long interval;
  long previousMillis;
  CallbackFunc func;
};

#define numEvents 5
#define trigPin 4
#define echoPin 5

#define ONE_WIRE_BUS A2

#define STATE_NOT_IN_USE 0
#define STATE_IN_USE_UNKNOWN 1
#define STATE_IN_USE_POOPING 2
#define STATE_IN_USE_SEAT_UP 3
#define STATE_IN_USE_PEEING_STANDING 4
#define STATE_IN_USE_PEEING_SITTING 5
#define STATE_IN_USE_CLEANING 6
#define STATE_TRIGGERED 7
#define STATE_TRIGGERED_DOUBLE 8
#define STATE_OPERATOR_MENU 9
#define STATE_OPERATOR_MENU_VAR 10

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
LiquidCrystal lcd(7, 6, 11, 10, 9, 8);

byte buttonsState = 0;
float ambientTemperature = 0;
float distance = 0;
byte wallDists = 0;
byte seatDists = 0;
bool motion = false;
int lightLevel = 0;
bool seatUp = false;
int remainingSprays = 0;
byte currentState = STATE_NOT_IN_USE;
int stateEntered = millis();
byte menuIndex = 0;
bool menuLoading = false;

String menuStrings[] {
  "Number 1 delay",
  "Number 2 delay",
  "Manual delay",
  "Back"
};

bool seatDistance() {
  return (((seatDists & 1) + ((seatDists & B10) >> 1) + ((seatDists & B100) >> 2) + ((seatDists & B1000) >> 3) + ((seatDists & B10000) >> 4)) > 3);// if at least 3 of last 5 satisfy the seat distance constraint, return true
}

scheduledEvent eventArray[numEvents] = { // stores all scheduled events
  {2000, 0, []() {  // temperature sensor-related interval
    sensors.requestTemperatures(); // Send the command to get temperatures
    ambientTemperature = sensors.getTempCByIndex(0);
    lcd.setCursor(0,0);
    lcd.print(String(ambientTemperature) + " C"); // memory-heavy operation
  }},
  {200, 0, []() {  // distance sensor-related interval
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    distance = pulseIn(echoPin, HIGH)*0.034/2; //in cm
    wallDists <<= 1;
    seatDists <<= 1;
    if (distance > 100) { // far enough from the sensor to be seen as wall
      wallDists |= 1;
    }
    if (distance < 70) { // close enough to sensor to be seen as sitting on the seat
      seatDists |= 1;
    }
  }},
  {300, 0, []() { // motion sensor-related interval
    motion = digitalRead(A0);
    if (motion) {
      if (currentState == STATE_NOT_IN_USE) {
        currentState = STATE_IN_USE_UNKNOWN;
        stateEntered = millis();
      }
    }
  }},
  {300, 0, []() { // light sensor-related interval
    int newLightLevel = analogRead(A1);
    if (newLightLevel - lightLevel > 150) {
      if (currentState == STATE_NOT_IN_USE) {
        currentState = STATE_IN_USE_UNKNOWN;
        stateEntered = millis();
      }
    } else if (lightLevel - newLightLevel > 150) {
      if (currentState == STATE_IN_USE_POOPING) {
        currentState = STATE_TRIGGERED_DOUBLE; // user is done pooping
      } else if (currentState == STATE_IN_USE_CLEANING) {
        currentState = STATE_NOT_IN_USE; // user is done cleaning
      } else if (currentState == STATE_IN_USE_PEEING_SITTING) {
        currentState = STATE_TRIGGERED; // user is done peeing sitting
      }

      if (currentState == STATE_IN_USE_SEAT_UP) {
        currentState = STATE_IN_USE_PEEING_STANDING;
      }
    }
    lightLevel = newLightLevel; // 0.3 * lightLevel + 0.7 * newLightLevel;
  }},
  {300, 0, []() { // magnetic sensor-related interval
    bool newSeatUp = digitalRead(A3);
    if (newSeatUp != seatUp) {
      if (newSeatUp && (currentState == STATE_NOT_IN_USE || currentState == STATE_IN_USE_UNKNOWN || currentState == STATE_IN_USE_POOPING)) {
        currentState = STATE_IN_USE_SEAT_UP;
        stateEntered = millis();
      } else if (currentState == STATE_NOT_IN_USE) {
        currentState = STATE_IN_USE_UNKNOWN;
        stateEntered = millis();
      }
    }
    seatUp = newSeatUp;
  }}
};

unsigned long currentMillis = 0;
byte ledState = 0;

void setup() {
  pinMode(12, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // put your setup code here, to run once:
  lcd.begin(16, 2);
  sensors.begin();
  Serial.begin(9600);
  remainingSprays += EEPROM.read(0) << 8;
  remainingSprays += EEPROM.read(1);

  displayRemainingSprays();

  attachInterrupt(digitalPinToInterrupt(2), sprayInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(3), menuInterrupt, FALLING);
}

void displayRemainingSprays() {
  lcd.setCursor(0, 1);
  lcd.print(F("Remaining:"));
  lcd.setCursor(10, 1);
  lcd.print(remainingSprays);
}

void loop() {
  buttonsState = 0;
  buttonsState += (1 - digitalRead(A4)) << 2;
  buttonsState += (1 - digitalRead(3)) << 1;
  buttonsState += 1 - digitalRead(2);
    
  eventLoop();

  switch (currentState) {
    case STATE_IN_USE_SEAT_UP:
      if (millis() - stateEntered >= 180000) {
        currentState = STATE_IN_USE_CLEANING;
      }
      break;
    case STATE_IN_USE_UNKNOWN:
      if (millis() - stateEntered >= 120000) {
        currentState = STATE_IN_USE_PEEING_SITTING;
      }
      break;
    case STATE_IN_USE_PEEING_STANDING:
      currentState = STATE_TRIGGERED;
      break;
    case STATE_TRIGGERED_DOUBLE:
      spray();
      currentState = STATE_TRIGGERED;
      break;
    case STATE_TRIGGERED:
      spray();
      currentState = STATE_NOT_IN_USE;
      break;
    case STATE_OPERATOR_MENU:
      if (menuLoading) {
        renderMenu();
        menuLoading = false;
      }
      if (digitalRead(A4)) {
        menuIndex--;
        menuLoading = true;
      } else if (digitalRead(2)) {
        menuIndex++;
        menuLoading = true;
      }
    default:
      break;
  }
}

void renderMenu() {
  byte dispOffset = 0;
  if (menuIndex == 3) {
    dispOffset = 1;
  }
  lcd.setCursor(0, dispOffset);
  lcd.print('>');
  lcd.setCursor(0, 1-dispOffset);
  lcd.print(' ');
  lcd.setCursor(1, 0);
  lcd.print(menuStrings[menuIndex - dispOffset]);
  lcd.setCursor(1, 1);
  lcd.print(menuStrings[menuIndex + 1 - dispOffset]);
}

void eventLoop() { // handles running of all scheduled events
  currentMillis = millis();
  for (byte i = 0; i < numEvents; i++) {
    if (currentMillis - eventArray[i].previousMillis >= eventArray[i].interval){ // rollover-safe time comparison
      eventArray[i].previousMillis = currentMillis;
      eventArray[i].func();
    }
  }
}

void spray() {
  detachInterrupt(digitalPinToInterrupt(2)); // detach the interrupts. We can't disable them fully because we use delays, which are dependent upon interrupts
  detachInterrupt(digitalPinToInterrupt(3));
  digitalWrite(12, HIGH);
  delay(700);
  digitalWrite(12, LOW);
  attachInterrupt(digitalPinToInterrupt(2), sprayInterrupt, FALLING); // dangerous section that can break the motor is over
  attachInterrupt(digitalPinToInterrupt(3), menuInterrupt, FALLING);
  remainingSprays--;
  EEPROM.update(0, remainingSprays >> 8);
  EEPROM.update(1, remainingSprays);
}

int lastSprayInterrupt = 0;

void sprayInterrupt() {
  int currTime = millis();
  if (currTime - lastSprayInterrupt > 100) { // debounce
      currentState = STATE_TRIGGERED;
  }
  lastSprayInterrupt = currTime;
}

int lastMenuInterrupt = 0;

void menuInterrupt() {
  int currTime = millis();
  detachInterrupt(digitalPinToInterrupt(2));
  detachInterrupt(digitalPinToInterrupt(3));
  if (currTime - lastMenuInterrupt > 100) { // debounce
    menuLoading = true;
    currentState = STATE_OPERATOR_MENU;
  }
  lastMenuInterrupt = currTime;
}
