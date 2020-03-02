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

#define NUM_MENU_ITEMS 5
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
long stateEntered = millis();
byte menuIndex = 0;
bool menuLoading = false;
bool dispLoading = true;
byte ledOn = 1;
bool lastLeftState = false;
long leftLastChanged = millis();
bool lastMiddleState = false;
long middleLastChanged = millis();
bool lastRightState = false;
long rightLastChanged = millis();
long currTime = millis();
byte sprayMode = 0; // 0 = pee, 1 = poo, 2 = manual

const String menuStrings[] {
  "Number 1 delay  ",
  "Number 2 delay  ",
  "Manual delay    ",
  "Reset shots     ",
  "Back            "
};


int readEEPROMInt(byte addr) {
  return (EEPROM.read(addr) << 8) + EEPROM.read(addr + 1);
}

void updateEEPROMInt(byte addr, int val){
  EEPROM.update(addr, val >> 8);
  EEPROM.update(addr + 1, val);
}

int configValues[] {
  readEEPROMInt(2), // number 1 delay
  readEEPROMInt(4), // number 2 delay
  readEEPROMInt(6)  // manual override delay
};

bool seatDistance() {
  return (((seatDists & 1) + ((seatDists & B10) >> 1) + ((seatDists & B100) >> 2) + ((seatDists & B1000) >> 3) + ((seatDists & B10000) >> 4)) > 3);// if at least 3 of last 5 satisfy the seat distance constraint, return true
}

scheduledEvent eventArray[numEvents] = { // stores all scheduled events
  {2000, 0, []() {  // temperature sensor-related interval
    sensors.requestTemperatures(); // Send the command to get temperatures
    ambientTemperature = sensors.getTempCByIndex(0);
    if (currentState != STATE_OPERATOR_MENU && currentState != STATE_OPERATOR_MENU_VAR){
      lcd.setCursor(0,0);
      lcd.print(String(ambientTemperature) + " C"); // memory-heavy operation
    }
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
        sprayMode = 1;
        currentState = STATE_TRIGGERED; // user is done pooping
      } else if (currentState == STATE_IN_USE_CLEANING) {
        currentState = STATE_NOT_IN_USE; // user is done cleaning
      } else if (currentState == STATE_IN_USE_PEEING_SITTING) {
        sprayMode = 0;
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
  // default EEPROM values
  if (readEEPROMInt(0) < 0) {
    updateEEPROMInt(0, 2400);
  }
  if (readEEPROMInt(2) < 0) {
    configValues[0] = 0;
    updateEEPROMInt(2, 0);
  }
  if (readEEPROMInt(4) < 0) {
    configValues[1] = 0;
    updateEEPROMInt(2, 0);
  }
  if (readEEPROMInt(6) < 0) {
    configValues[2] = 0;
    updateEEPROMInt(2, 0);
  }
  pinMode(13, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // put your setup code here, to run once:
  lcd.begin(16, 2);
  sensors.begin();
  Serial.begin(9600);
  remainingSprays = readEEPROMInt(0);

  attachInterrupt(digitalPinToInterrupt(2), sprayInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(3), menuInterrupt, FALLING);
}

void displayRemainingSprays() {
  if (currentState != STATE_OPERATOR_MENU && currentState != STATE_OPERATOR_MENU_VAR) {
    lcd.setCursor(0, 1);
    lcd.print(F("Remaining:"));
    lcd.setCursor(10, 1);
    lcd.print(remainingSprays);
  }
}

void loop() {
  buttonsState = 0;
  buttonsState += (1 - digitalRead(A4)) << 2;
  buttonsState += (1 - digitalRead(3)) << 1;
  buttonsState += 1 - digitalRead(2);
    
  eventLoop();

  if (dispLoading) {
    displayRemainingSprays();
    dispLoading = false;
  }

  digitalWrite(13, (ledOn = 1 - ledOn));

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
      sprayMode = 0;
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
      currTime = millis();
      if (currTime - leftLastChanged > 100) { // otherwise not interested in this value
        bool leftState = !digitalRead(A4); // is left button pressed?
        if (leftState != lastLeftState) { // state changed
          leftLastChanged = currTime;
          if (leftState) { // state is pressed (so the button just got pressed)
            menuIndex = max(0, menuIndex - 1);
            menuLoading = true;
          }
        }
        lastLeftState = leftState;
      }
      if (currTime - middleLastChanged > 100) { // otherwise not interested in this value
        bool middleState = !digitalRead(3); // is middle button pressed?
        if (middleState != lastMiddleState) { // state changed
          middleLastChanged = currTime;
          if (middleState) { // state is pressed (so the button just got pressed)
            currentState = STATE_OPERATOR_MENU_VAR;
            menuLoading = true;
          }
        }
        lastMiddleState = middleState;
      }
      if (currTime - rightLastChanged > 100) { // otherwise not interested in this value
        bool rightState = !digitalRead(2); // is right button pressed?
        if (rightState != lastRightState) { // state changed
          rightLastChanged = currTime;
          if (rightState) { // state is pressed (so the button just got pressed)
            menuIndex = min(NUM_MENU_ITEMS - 1, menuIndex + 1);
            menuLoading = true;
          }
        }
        lastRightState = rightState;
      }
      break;
    case STATE_OPERATOR_MENU_VAR:
      if (menuIndex == NUM_MENU_ITEMS - 2) { // reset sprays button
        remainingSprays = 2400;
        updateEEPROMInt(0, remainingSprays);
        menuIndex = NUM_MENU_ITEMS - 1; // exit from menu
      }
      if (menuIndex == NUM_MENU_ITEMS - 1) { // back button
        lcd.clear(); // clear so the display doesn't retain junk on the screen
        dispLoading = true; // loading flag so the remaining shots get rendered again
        menuLoading = true;
        currentState = STATE_NOT_IN_USE; // exit out of menu
        Serial.println("Exiting menu...");
        attachInterrupt(digitalPinToInterrupt(2), sprayInterrupt, FALLING); // enable interrupts again
        attachInterrupt(digitalPinToInterrupt(3), menuInterrupt, FALLING);
        break;
      }
      if (menuLoading) {
        renderVarMenu();
        menuLoading = false;
      }
      currTime = millis();
      if (currTime - leftLastChanged > 100) { // otherwise not interested in this value
        bool leftState = !digitalRead(A4); // is left button pressed?
        if (leftState != lastLeftState) { // state changed
          leftLastChanged = currTime;
          if (leftState) { // state is pressed (so the button just got pressed)
            configValues[menuIndex] -= 100;
            menuLoading = true;
          }
        }
        lastLeftState = leftState;
      }
      if (currTime - middleLastChanged > 100) { // otherwise not interested in this value
        bool middleState = !digitalRead(3); // is middle button pressed?
        if (middleState != lastMiddleState) { // state changed
          middleLastChanged = currTime;
          if (middleState) { // state is pressed (so the button just got pressed)
            lcd.clear();
            updateEEPROMInt((menuIndex + 1) * 2, configValues[menuIndex]);
            currentState = STATE_OPERATOR_MENU;
            menuLoading = true;
          }
        }
        lastMiddleState = middleState;
      }
      if (currTime - rightLastChanged > 100) { // otherwise not interested in this value
        bool rightState = !digitalRead(2); // is right button pressed?
        if (rightState != lastRightState) { // state changed
          rightLastChanged = currTime;
          if (rightState) { // state is pressed (so the button just got pressed)
            configValues[menuIndex] += 100;
            menuLoading = true;
          }
        }
        lastRightState = rightState;
      }
      break;
    default:
      break;
  }
}

void renderMenu() {
  Serial.println("Rendering menu");
  byte dispOffset = 0;
  if (menuIndex == NUM_MENU_ITEMS - 1) {
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

void renderVarMenu() {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print(configValues[menuIndex]);
  lcd.setCursor(0, 1);
  lcd.print(F(" <     OK     > "));
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
  Serial.print("Spraying, mode = ");
  Serial.println(sprayMode);
  detachInterrupt(digitalPinToInterrupt(2)); // detach the interrupts. We can't disable them fully because we use delays, which are dependent upon interrupts
  detachInterrupt(digitalPinToInterrupt(3));
  delay(configValues[sprayMode]);
  digitalWrite(12, HIGH);
  delay(700);
  digitalWrite(12, LOW);
  remainingSprays--;
  if (sprayMode == 1) { // poop, so double spray
    delay(2000); // make sure the motor gets time to reset
    digitalWrite(12, HIGH);
    delay(700);
    digitalWrite(12, LOW);
    remainingSprays--;
  }
  attachInterrupt(digitalPinToInterrupt(2), sprayInterrupt, FALLING); // dangerous section - that can break the motor - is over
  attachInterrupt(digitalPinToInterrupt(3), menuInterrupt, FALLING);
  updateEEPROMInt(0, remainingSprays);
  displayRemainingSprays();
}



void sprayInterrupt() {
  long currTime = millis();
  if (currTime - rightLastChanged > 100) { // debounce
    sprayMode = 2; // manual mode
    currentState = STATE_TRIGGERED;
  }
  rightLastChanged = currTime;
}

void menuInterrupt() {
  long myCurrTime = millis();
  if (myCurrTime - middleLastChanged > 100) { // debounce
    detachInterrupt(digitalPinToInterrupt(2));
    detachInterrupt(digitalPinToInterrupt(3));
    menuLoading = true;
    lastMiddleState = true;
    currentState = STATE_OPERATOR_MENU;
  }
  middleLastChanged = myCurrTime;
}
