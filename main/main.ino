#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>

int ldrPin = A0;
int lightThreshold = 50;
int btnPin = 2;  
int buzzerPin = 7;

int maxBuffSize = 11;
int logHead = 0, logTail = 0;

bool authorised = false;
String cmd = "";   
bool received_cmd = false;

bool awake = false; 

typedef struct {
  uint8_t ldrValue;
  bool pushButtonStatus;
  bool intrusionStatus;
} status;

typedef struct {
  status data;
  uint32_t timestamp;
} LogEvent;

LogEvent LogBuffer[11];

void alert() {
  tone(buzzerPin, 2000, 200); 
}

void reset() {
  noTone(buzzerPin);
}

bool bufferFull() {
  return ((logHead + 1) % maxBuffSize) == logTail;
}

void popBuffer() {
  logTail = (logTail + 1) % maxBuffSize;
}

void pushBuffer(LogEvent log) {
  LogBuffer[logHead] = log;
  logHead = (logHead + 1) % maxBuffSize;
}

void log(status data) {
  if (bufferFull()) popBuffer();
  LogEvent logEntry;
  logEntry.data = data;
  logEntry.timestamp = millis();
  pushBuffer(logEntry);
}

void check_intrusion(status *data) {
  data->ldrValue = analogRead(ldrPin);
  data->pushButtonStatus = (digitalRead(btnPin) == LOW);  // Active LOW
  data->intrusionStatus = (!authorised && data->ldrValue > lightThreshold && !data->pushButtonStatus);

  Serial.print("[DEBUG] LDR: ");
  Serial.print(data->ldrValue);
  Serial.print(" | Drawer opened: ");
  Serial.print(data->pushButtonStatus ? "NO" : "YES");
  Serial.print(" | Intrusion: ");
  Serial.println(data->intrusionStatus ? "YES" : "NO");

  if (data->intrusionStatus) alert();
  else reset();
}

void printLogs() {
  Serial.println("=== Log Dump ===");
  int index = logTail;
  while (index != logHead) {
    Serial.print("[");
    Serial.print(LogBuffer[index].timestamp);
    Serial.print("] LDR: ");
    Serial.print(LogBuffer[index].data.ldrValue);
    Serial.print(" | Intrusion: ");
    Serial.println(LogBuffer[index].data.intrusionStatus ? "YES" : "NO");
    index = (index + 1) % maxBuffSize;
  }
}

void onDrawerStateChange() {
  if (digitalRead(btnPin) == HIGH) {  // button not pressed ==> drawer is open
    awake = true;
  } else {
    awake = false;     // when pressed:  HIGH → LOW → HIGH → LOW → (stabilizes at LOW) and intrusion also depends on light intensity ==> debounce is not necessary in our case
  }
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      received_cmd = true;
    } else {
      cmd += inChar;
    }
  }
}


void setup() {
  Serial.begin(9600);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ldrPin, INPUT);
  pinMode(btnPin, INPUT_PULLUP);  // Active LOW button

  attachInterrupt(digitalPinToInterrupt(btnPin), onDrawerStateChange, CHANGE);

  cmd.reserve(100);
}

void loop() {
  if (received_cmd) {
    cmd.trim(); 

    if (cmd.equalsIgnoreCase("AUTH YES")) {
      authorised = true;
      Serial.println("Authorization enabled.");
    } 
    else if (cmd.equalsIgnoreCase("AUTH NO")) {
      authorised = false;
      Serial.println("Authorization disabled.");
    } 
    else if (cmd.equalsIgnoreCase("LOG")) {
      printLogs();
    } 
    else {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
    }
    cmd = "";
    received_cmd = false;
  }

  if (awake) {
    status data;
    check_intrusion(&data);
    log(data);
    delay(1000);  
  } else {
    // set_sleep_mode(SLEEP_MODE_PWR_DOWN); // shuts everything
    set_sleep_mode(SLEEP_MODE_IDLE); // keeps the serial and timers running
    noInterrupts();
    sleep_enable();
    EIFR = bit(INTF0);  // clear any pending interrupt
    interrupts();
    sleep_cpu();      // Arduino sleeps here
    sleep_disable();  // resumes after wake
  }
}
