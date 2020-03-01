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

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
LiquidCrystal lcd(7, 6, 11, 10, 9, 8);

byte buttonsState = 0;
float ambientTemperature = 0;
float distance = 0;
bool motion = false;
int lightLevel = 0;
bool seatUp = false;
int remainingSprays = 0;

scheduledEvent eventArray[numEvents] = { // stores all scheduled events
  {2000, 0, []() {
    sensors.requestTemperatures(); // Send the command to get temperatures
    ambientTemperature = sensors.getTempCByIndex(0);
    lcd.setCursor(0,0);
    lcd.print(String(ambientTemperature) + " C"); // memory-heavy operation
  }},
  {200, 0, []() {
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    distance = pulseIn(echoPin, HIGH)*0.034/2; //in cm
  }},
  {300, 0, []() {
    motion = digitalRead(A0);
  }},
  {300, 0, []() {
    lightLevel = analogRead(A1);
  }},
  {300, 0, []() {
    seatUp = digitalRead(A3);
  }}
};

unsigned long currentMillis = 0;
byte ledState = 0;

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  // pinMode(12, OUTPUT);
//  int numSprays = 2385;
//  EEPROM.update(0, numSprays >> 8);
//  EEPROM.update(1, numSprays);
//
//  int readSprays = 0;
//  readSprays += EEPROM.read(0) << 8;
//  readSprays += EEPROM.read(1);

  // put your setup code here, to run once:
  lcd.begin(16, 2);
  sensors.begin();
  Serial.begin(9600);
  remainingSprays += EEPROM.read(0) << 8;
  remainingSprays += EEPROM.read(1);

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

  Serial.println(buttonsState);
  
  eventLoop();
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
