#include <Arduino.h>

// Firmware version
const String version = "20251031";

// Analog lever pins
const int autoBrakePin    = A5;
const int indyBrakePin    = A4;
const int dynamicBrakePin = A6;
const int throttlePin     = A3;
const int reverserPin     = A7;

// Shared multiplexer address pins
const int S0 = 2;
const int S1 = 3;
const int S2 = 4;
const int S3 = 5;

// Multiplexer signal pins
const int mux1SIG = 6;
const int mux2SIG = 7;

// Keypad column pins (driven directly)
const int kpC1 = 8;
const int kpC2 = 9;
const int kpC3 = 10;
const int kpC4 = 11;
const int kpCols[4] = { kpC1, kpC2, kpC3, kpC4 };

// Direct digital input pins

const int alerterPin = 12;
const int hornPin    = 13;

// MUX1 channel mapping
const uint8_t M1_CONTROL      = 0;
const uint8_t M1_GEN_FIELD    = 1;
const uint8_t M1_ENGINE_RUN   = 2;
const uint8_t M1_BELL         = 3;
const uint8_t M1_SAND         = 4;
const uint8_t M1_FRONT_HIGH   = 9;
const uint8_t M1_FRONT_LOW    = 10;
const uint8_t M1_FRONT_OFF    = 11;
const uint8_t M1_REAR_HIGH    = 12;
const uint8_t M1_REAR_LOW     = 13;
const uint8_t M1_REAR_OFF     = 14;
const uint8_t M1_BAIL         = 15;

// MUX2 channel mapping
const uint8_t M2_KP_R4         = 0;
const uint8_t M2_KP_R3         = 1;
const uint8_t M2_KP_R2         = 2;
const uint8_t M2_KP_R1         = 3;
const uint8_t M2_LIGHT_GAUGE   = 4;
const uint8_t M2_LIGHT_CAB     = 5;
const uint8_t M2_HANDBRAKE     = 6;
const uint8_t M2_WIPER         = 7;
const uint8_t M2_COUNTER_UP    = 8;
const uint8_t M2_COUNTER_DOWN  = 9;
const uint8_t M2_SLOW_SPEED    = 10;
const uint8_t M2_DPU_DYN       = 11;
const uint8_t M2_DPU_THR_DEC   = 12;
const uint8_t M2_DPU_THR_INC   = 13;
const uint8_t M2_DPU_FENCE_DEC = 14;
const uint8_t M2_DPU_FENCE_INC = 15;

// State variables
int autoVal = 0, indyVal = 0, dynVal = 0, thrVal = 0, revVal = 0;
int counterPos = 0, dpuFencePos = 0, dpuThrPos = 0;
int dpuDynVal = 0, handbrakeVal = 0, wiperVal = 0;
int lightGaugeVal = 0, lightCabVal = 0, engineRunVal = 0, genFieldVal = 0, controlVal = 1, bailVal = 1;
int frontHeadlightPos = 0, rearHeadlightPos = 0;
bool sandVal = false, bellVal = false, alerterVal = false, hornVal = false, slowSpeedVal = false;

// Keypad latch values
int key_0 = 0, key_1 = 0, key_2 = 0, key_3 = 0, key_4 = 0, key_5 = 0, key_6 = 0, key_7 = 0, key_8 = 0, key_9 = 0;
int key_star = 0, key_pound = 0, key_A = 0, key_B = 0, key_C = 0, key_D = 0;

int incomingByte = 0;

// Debounce tracking
const unsigned long debounce_filter = 100;
const unsigned long analog_filter = 50;
unsigned long sand_last = 0, bell_last = 0, alerter_last = 0, horn_last = 0;
unsigned long mux1_debounce[16] = {0};
unsigned long mux2_debounce[16] = {0};
unsigned long frontHeadlight_last = 0, rearHeadlight_last = 0;

bool sand_prev = false, bell_prev = false, alerter_prev = false, horn_prev = false;
int prev_counterPos = -1, prev_dpuFencePos = -1, prev_dpuThrPos = -1;
bool prev_dpuDyn = false, prev_slowSpeed = false, prev_handbrake = false, prev_wiper = false;
bool prev_lightGauge = false, prev_lightCab = false, prev_engineRun = false, prev_genField = false;
int prev_control = -1, prev_bail = -1;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

inline void muxSelect(uint8_t channel) {
  digitalWrite(S0, channel & 0x01);
  digitalWrite(S1, (channel >> 1) & 0x01);
  digitalWrite(S2, (channel >> 2) & 0x01);
  digitalWrite(S3, (channel >> 3) & 0x01);
}

inline int readMux1(uint8_t channel) {
  muxSelect(channel);
  delayMicroseconds(5);
  return digitalRead(mux1SIG);
}

inline int readMux2(uint8_t channel) {
  muxSelect(channel);
  delayMicroseconds(5);
  return digitalRead(mux2SIG);
}

char keyAt(uint8_t row, uint8_t col) {
  static const char keys[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
  };
  return keys[row][col];
}

void setKeyValue(char key, int pressed) {
  switch (key) {
    case '0': key_0 = pressed; break;
    case '1': key_1 = pressed; break;
    case '2': key_2 = pressed; break;
    case '3': key_3 = pressed; break;
    case '4': key_4 = pressed; break;
    case '5': key_5 = pressed; break;
    case '6': key_6 = pressed; break;
    case '7': key_7 = pressed; break;
    case '8': key_8 = pressed; break;
    case '9': key_9 = pressed; break;
    case '*': key_star = pressed; break;
    case '#': key_pound = pressed; break;
    case 'A': key_A = pressed; break;
    case 'B': key_B = pressed; break;
    case 'C': key_C = pressed; break;
    case 'D': key_D = pressed; break;
  }
}

void scanKeypadLatched() {
  const uint8_t rowChannels[4] = { M2_KP_R1, M2_KP_R2, M2_KP_R3, M2_KP_R4 };

  for (int col = 0; col < 4; col++) {
    for (int j = 0; j < 4; j++) {
      digitalWrite(kpCols[j], HIGH);
    }
    digitalWrite(kpCols[col], LOW);
    delayMicroseconds(10);

    for (int row = 0; row < 4; row++) {
      int pressed = !readMux2(rowChannels[row]);
      setKeyValue(keyAt(row, col), pressed ? 1 : 0);
    }
  }

  for (int j = 0; j < 4; j++) {
    digitalWrite(kpCols[j], HIGH);
  }
}

void updateHeadlightPosition(int &storedPos, unsigned long &lastChange, uint8_t offCh, uint8_t lowCh, uint8_t highCh, unsigned long now) {
  int candidate = -1;
  if (!readMux1(offCh))      candidate = 0;
  else if (!readMux1(lowCh)) candidate = 1;
  else if (!readMux1(highCh))candidate = 2;

  if (candidate != -1 && candidate != storedPos && ((now - lastChange) > debounce_filter || lastChange == 0)) {
    storedPos = candidate;
    lastChange = now;
  }
}

// -----------------------------------------------------------------------------
// Setup / Loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(9600);

  pinMode(autoBrakePin,    INPUT);
  pinMode(indyBrakePin,    INPUT);
  pinMode(dynamicBrakePin, INPUT);
  pinMode(throttlePin,     INPUT);
  pinMode(reverserPin,     INPUT);

  pinMode(alerterPin, INPUT_PULLUP);
  pinMode(hornPin,    INPUT_PULLUP);

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  pinMode(mux1SIG, INPUT_PULLUP);
  pinMode(mux2SIG, INPUT_PULLUP);

  for (int i = 0; i < 4; i++) {
    pinMode(kpCols[i], OUTPUT);
    digitalWrite(kpCols[i], HIGH);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  unsigned long now = millis();

  {
    static bool init = false;
    static unsigned long lastTime = 0;
    int reading = analogRead(autoBrakePin);
    if (!init) {
      autoVal = reading;
      init = true;
      lastTime = now;
    }
    if (reading != autoVal) {
      if ((now - lastTime) > analog_filter) {
        autoVal = reading;
        lastTime = now;
      }
    } else {
      lastTime = now;
    }
  }

  {
    static bool init = false;
    static unsigned long lastTime = 0;
    int reading = analogRead(indyBrakePin);
    if (!init) {
      indyVal = reading;
      init = true;
      lastTime = now;
    }
    if (reading != indyVal) {
      if ((now - lastTime) > analog_filter) {
        indyVal = reading;
        lastTime = now;
      }
    } else {
      lastTime = now;
    }
  }

  {
    static bool init = false;
    static unsigned long lastTime = 0;
    int reading = analogRead(dynamicBrakePin);
    if (!init) {
      dynVal = reading;
      init = true;
      lastTime = now;
    }
    if (reading != dynVal) {
      if ((now - lastTime) > analog_filter) {
        dynVal = reading;
        lastTime = now;
      }
    } else {
      lastTime = now;
    }
  }

  {
    static bool init = false;
    static unsigned long lastTime = 0;
    int reading = analogRead(throttlePin);
    if (!init) {
      thrVal = reading;
      init = true;
      lastTime = now;
    }
    if (reading != thrVal) {
      if ((now - lastTime) > analog_filter) {
        thrVal = reading;
        lastTime = now;
      }
    } else {
      lastTime = now;
    }
  }

  {
    static bool init = false;
    static unsigned long lastTime = 0;
    int reading = analogRead(reverserPin);
    if (!init) {
      revVal = reading;
      init = true;
      lastTime = now;
    }
    if (reading != revVal) {
      if ((now - lastTime) > analog_filter) {
        revVal = reading;
        lastTime = now;
      }
    } else {
      lastTime = now;
    }
  }

  bool sand_cur = !readMux1(M1_SAND);
  if ((now - sand_last) > debounce_filter || sand_last == 0 || sand_cur != sand_prev) {
    sandVal = sand_cur;
    sand_prev = sand_cur;
    sand_last = now;
  }

  bool bell_cur = !readMux1(M1_BELL);
  if ((now - bell_last) > debounce_filter || bell_last == 0 || bell_cur != bell_prev) {
    bellVal = bell_cur;
    bell_prev = bell_cur;
    bell_last = now;
  }

  bool alerter_cur = digitalRead(alerterPin) == LOW;
  if ((now - alerter_last) > debounce_filter || alerter_last == 0 || alerter_cur != alerter_prev) {
    alerterVal = alerter_cur;
    alerter_prev = alerter_cur;
    alerter_last = now;
  }

  bool horn_cur = digitalRead(hornPin) == LOW;
  if ((now - horn_last) > debounce_filter || horn_last == 0 || horn_cur != horn_prev) {
    hornVal = horn_cur;
    horn_prev = horn_cur;
    horn_last = now;
  }

  int counter_cur = 0;
  if (!readMux2(M2_COUNTER_UP))        counter_cur = 1;
  else if (!readMux2(M2_COUNTER_DOWN)) counter_cur = 2;
  if (counter_cur != prev_counterPos && ((now - mux2_debounce[M2_COUNTER_UP]) > debounce_filter || mux2_debounce[M2_COUNTER_UP] == 0)) {
    counterPos = counter_cur;
    prev_counterPos = counter_cur;
    mux2_debounce[M2_COUNTER_UP] = now;
    mux2_debounce[M2_COUNTER_DOWN] = now;
  }

  int dpuFence_cur = 0;
  if (!readMux2(M2_DPU_FENCE_INC))      dpuFence_cur = 1;
  else if (!readMux2(M2_DPU_FENCE_DEC)) dpuFence_cur = 2;
  if (dpuFence_cur != prev_dpuFencePos && ((now - mux2_debounce[M2_DPU_FENCE_INC]) > debounce_filter || mux2_debounce[M2_DPU_FENCE_INC] == 0)) {
    dpuFencePos = dpuFence_cur;
    prev_dpuFencePos = dpuFence_cur;
    mux2_debounce[M2_DPU_FENCE_INC] = now;
    mux2_debounce[M2_DPU_FENCE_DEC] = now;
  }

  int dpuThr_cur = 0;
  if (!readMux2(M2_DPU_THR_INC))      dpuThr_cur = 1;
  else if (!readMux2(M2_DPU_THR_DEC)) dpuThr_cur = 2;
  if (dpuThr_cur != prev_dpuThrPos && ((now - mux2_debounce[M2_DPU_THR_INC]) > debounce_filter || mux2_debounce[M2_DPU_THR_INC] == 0)) {
    dpuThrPos = dpuThr_cur;
    prev_dpuThrPos = dpuThr_cur;
    mux2_debounce[M2_DPU_THR_INC] = now;
    mux2_debounce[M2_DPU_THR_DEC] = now;
  }

  bool dpuDyn_cur = !readMux2(M2_DPU_DYN);
  if (dpuDyn_cur != prev_dpuDyn && ((now - mux2_debounce[M2_DPU_DYN]) > debounce_filter || mux2_debounce[M2_DPU_DYN] == 0)) {
    dpuDynVal = dpuDyn_cur ? 1 : 0;
    prev_dpuDyn = dpuDyn_cur;
    mux2_debounce[M2_DPU_DYN] = now;
  }

  bool slowSpeed_cur = !readMux2(M2_SLOW_SPEED);
  if (slowSpeed_cur != prev_slowSpeed && ((now - mux2_debounce[M2_SLOW_SPEED]) > debounce_filter || mux2_debounce[M2_SLOW_SPEED] == 0)) {
    slowSpeedVal = slowSpeed_cur;
    prev_slowSpeed = slowSpeed_cur;
    mux2_debounce[M2_SLOW_SPEED] = now;
  }

  bool handbrake_cur = !readMux2(M2_HANDBRAKE);
  if (handbrake_cur != prev_handbrake && ((now - mux2_debounce[M2_HANDBRAKE]) > debounce_filter || mux2_debounce[M2_HANDBRAKE] == 0)) {
    handbrakeVal = handbrake_cur ? 1 : 0;
    prev_handbrake = handbrake_cur;
    mux1_debounce[M2_HANDBRAKE] = now;
  }

  bool wiper_cur = !readMux2(M2_WIPER);
  if (wiper_cur != prev_wiper && ((now - mux2_debounce[M2_WIPER]) > debounce_filter || mux2_debounce[M2_WIPER] == 0)) {
    wiperVal = wiper_cur ? 1 : 0;
    prev_wiper = wiper_cur;
    mux1_debounce[M2_WIPER] = now;
  }

  bool lightGauge_cur = !readMux2(M2_LIGHT_GAUGE );
  if (lightGauge_cur != prev_lightGauge && ((now - mux2_debounce[M2_LIGHT_GAUGE ]) > debounce_filter || mux2_debounce[M2_LIGHT_GAUGE ] == 0)) {
    lightGaugeVal = lightGauge_cur ? 1 : 0;
    prev_lightGauge = lightGauge_cur;
    mux1_debounce[M2_LIGHT_GAUGE ] = now;
  }

  bool lightCab_cur = !readMux2(M2_LIGHT_CAB);
  if (lightCab_cur != prev_lightCab && ((now - mux2_debounce[M2_LIGHT_CAB]) > debounce_filter || mux2_debounce[M2_LIGHT_CAB] == 0)) {
    lightCabVal = lightCab_cur ? 1 : 0;
    prev_lightCab = lightCab_cur;
    mux1_debounce[M2_LIGHT_CAB] = now;
  }

  bool engineRun_cur = !readMux1(M1_ENGINE_RUN);
  if (engineRun_cur != prev_engineRun && ((now - mux1_debounce[M1_ENGINE_RUN]) > debounce_filter || mux1_debounce[M1_ENGINE_RUN] == 0)) {
    engineRunVal = engineRun_cur ? 1 : 0;
    prev_engineRun = engineRun_cur;
    mux1_debounce[M1_ENGINE_RUN] = now;
  }

  bool genField_cur = !readMux1(M1_GEN_FIELD);
  if (genField_cur != prev_genField && ((now - mux1_debounce[M1_GEN_FIELD]) > debounce_filter || mux1_debounce[M1_GEN_FIELD] == 0)) {
    genFieldVal = genField_cur ? 1 : 0;
    prev_genField = genField_cur;
    mux1_debounce[M1_GEN_FIELD] = now;
  }

  int control_cur = readMux1(M1_CONTROL);
  if (control_cur != prev_control && ((now - mux1_debounce[M1_CONTROL]) > debounce_filter || mux1_debounce[M1_CONTROL] == 0)) {
    controlVal = control_cur;
    prev_control = control_cur;
    mux1_debounce[M1_CONTROL] = now;
  }

  int bail_cur = readMux1(M1_BAIL);
  if (bail_cur != prev_bail && ((now - mux1_debounce[M1_BAIL]) > debounce_filter || mux1_debounce[M1_BAIL] == 0)) {
    bailVal = bail_cur;
    prev_bail = bail_cur;
    mux1_debounce[M1_BAIL] = now;
  }

  updateHeadlightPosition(frontHeadlightPos, frontHeadlight_last, M1_FRONT_OFF, M1_FRONT_LOW, M1_FRONT_HIGH, now);
  updateHeadlightPosition(rearHeadlightPos,  rearHeadlight_last,  M1_REAR_OFF,  M1_REAR_LOW,  M1_REAR_HIGH,  now);

  scanKeypadLatched();

  if (Serial.available() > 0) {
    incomingByte = Serial.read();
    if (incomingByte == 'r') {  // read request
      Serial.print(autoVal);               Serial.print(',');
      Serial.print(indyVal);               Serial.print(',');
      Serial.print(dynVal);                Serial.print(',');
      Serial.print(thrVal);                Serial.print(',');
      Serial.print(revVal);                Serial.print(',');
      Serial.print(counterPos);            Serial.print(',');
      Serial.print(dpuFencePos);           Serial.print(',');
      Serial.print(dpuThrPos);             Serial.print(',');
      Serial.print(dpuDynVal);             Serial.print(',');
      Serial.print(slowSpeedVal);          Serial.print(',');
      Serial.print(handbrakeVal);          Serial.print(',');
      Serial.print(wiperVal);              Serial.print(',');
      Serial.print(sandVal);               Serial.print(',');
      Serial.print(bellVal);               Serial.print(',');
      Serial.print(alerterVal);            Serial.print(',');
      Serial.print(lightGaugeVal);         Serial.print(',');
      Serial.print(lightCabVal);           Serial.print(',');
      Serial.print(engineRunVal);          Serial.print(',');
      Serial.print(genFieldVal);           Serial.print(',');
      Serial.print(controlVal);            Serial.print(',');
      Serial.print(bailVal);               Serial.print(',');
      Serial.print(hornVal);               Serial.print(',');
      Serial.print(frontHeadlightPos);     Serial.print(',');
      Serial.print(rearHeadlightPos);      Serial.print(',');
      Serial.print(key_0);                 Serial.print(',');
      Serial.print(key_1);                 Serial.print(',');
      Serial.print(key_2);                 Serial.print(',');
      Serial.print(key_3);                 Serial.print(',');
      Serial.print(key_4);                 Serial.print(',');
      Serial.print(key_5);                 Serial.print(',');
      Serial.print(key_6);                 Serial.print(',');
      Serial.print(key_7);                 Serial.print(',');
      Serial.print(key_8);                 Serial.print(',');
      Serial.print(key_9);                 Serial.print(',');
      Serial.print(key_star);              Serial.print(',');
      Serial.print(key_pound);             Serial.print(',');
      Serial.print(key_A);                 Serial.print(',');
      Serial.print(key_B);                 Serial.print(',');
      Serial.print(key_C);                 Serial.print(',');
      Serial.println(key_D);
    }
    else if (incomingByte == 'I') { // Identification request
      Serial.print("miniRD,");
      Serial.println(version);
    }
  }
}
