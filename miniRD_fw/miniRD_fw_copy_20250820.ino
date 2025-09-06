#include <Arduino.h>

// Firmware version
const String version = "20250825";

// Analog break-point values for three-way switch inputs
const int low_bp = 250;
const int high_bp = 750;


// Analog lever pins
//const int autoBrakePin    = A3;
//const int indyBrakePin    = A1;
//const int dynamicBrakePin = A2;
//const int throttlePin     = A0;
//const int reverserPin     = A4;

// Pins for digital and mux
//const int sandPin         = A5; //these are analog pins being used for buttons
//const int bellPin         = A6; //these are analog pins being used for buttons
//const int alerterPin      = A7; //these are analog pins being used for buttons


// Analog lever pins
const int autoBrakePin    = A5;
const int indyBrakePin    = A4;
const int dynamicBrakePin = A6;
const int throttlePin     = A3;
const int reverserPin     = A7;

// Pins for digital and mux
const int sandPin         = A0; //these are analog pins being used for buttons
const int bellPin         = A1; //these are analog pins being used for buttons
const int alerterPin      = A2; //these are analog pins being used for buttons
const int muxS0           = 2;
const int muxS1           = 3;
const int muxS2           = 4;
const int muxS3           = 5;
const int muxSIG          = 6;
const int rearOffPin      = 7;
const int rearLowPin      = 8;
const int rearHighPin     = 9;
const int frontOffPin     = 10;
const int frontLowPin     = 11;
const int frontHighPin    = 12;
const int hornPin         = 13;

// State variables
int autoVal, indyVal, dynVal, thrVal, revVal;
int counterPos, dpuFencePos, dpuThrPos;
int dpuDynVal, handbrakeVal, wiperVal;
int lightGaugeVal, lightCabVal, engineRunVal, genFieldVal, controlVal, bailVal;
int frontHeadlightPos = 0, rearHeadlightPos = 0;
bool sandVal, bellVal, alerterVal, hornVal, slowSpeedVal;

// Timing/debounce variables
unsigned long debounce_filter = 100; // ms
unsigned long analog_filter   = 50; // ms

// For debounce: per input
unsigned long sand_last = 0, bell_last = 0, alerter_last = 0, horn_last = 0;
unsigned long mux_debounce[16] = {0}; // For all 16 mux channels
unsigned long frontHeadlight_last = 0, rearHeadlight_last = 0;

// For digital/analog buttons: previous state
bool sand_prev = false, bell_prev = false, alerter_prev = false, horn_prev = false;

// For MUX: previous state per logical control
int prev_counter_pos = -1, prev_dpuFencePos = -1, prev_dpuThrPos = -1;
bool prev_dpuDynVal = false, prev_handbrakeVal = false, prev_wiperVal = false, prev_slowSpeedVal = false;
bool prev_lightGaugeVal = false, prev_lightCabVal = false, prev_engineRunVal = false, prev_genFieldVal = false, prev_controlVal = false, prev_bailVal = false;

// Headlight rotary previous state
int prev_frontHeadlightPos = -1, prev_rearHeadlightPos = -1;

// Helper: read one digital channel from the mux
int readMux(int channel) {
  digitalWrite(muxS0, channel & 0x1);
  digitalWrite(muxS1, (channel >> 1) & 0x1);
  digitalWrite(muxS2, (channel >> 2) & 0x1);
  digitalWrite(muxS3, (channel >> 3) & 0x1);
  delayMicroseconds(5);
  return digitalRead(muxSIG);
}

int incomingByte = 0; // for incoming serial data

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Analog levers: A0–A4 are analog, so INPUT (not pullup)
  pinMode(autoBrakePin,    INPUT);
  pinMode(indyBrakePin,    INPUT);
  pinMode(dynamicBrakePin, INPUT);
  pinMode(throttlePin,     INPUT);
  pinMode(reverserPin,     INPUT);

  // Non-analog digital/mux pins: use INPUT_PULLUP
  pinMode(sandPin,    INPUT_PULLUP);
  pinMode(bellPin,    INPUT_PULLUP); // For backward compatibility, but now using analogRead for bell/alerter
  pinMode(alerterPin, INPUT_PULLUP);
  //pinMode(hornPin,    INPUT_PULLUP);

  pinMode(frontOffPin,  INPUT_PULLUP);
  pinMode(frontLowPin,  INPUT_PULLUP);
  pinMode(frontHighPin, INPUT_PULLUP);
  pinMode(rearOffPin,   INPUT_PULLUP);
  pinMode(rearLowPin,   INPUT_PULLUP);
  pinMode(rearHighPin,  INPUT_PULLUP);

  // Mux control pins: set as OUTPUT (not pullup)
  pinMode(muxS0, OUTPUT);
  pinMode(muxS1, OUTPUT);
  pinMode(muxS2, OUTPUT);
  pinMode(muxS3, OUTPUT);
  pinMode(muxSIG, INPUT_PULLUP); // SIG pin is read, should use pullup if floating
}

void loop() {
  unsigned long now = millis();

  // --- Analog levers (filtered for noise, as before) ---
  autoVal = analogRead(autoBrakePin);
  if (abs(autoVal - autoVal) > 2) {  // Ignore tiny changes (tweak as needed)
    static unsigned long aut_time = 0;
    if ((now - aut_time) > analog_filter) {
      aut_time = now;
    }
  }
  indyVal = analogRead(indyBrakePin);
  if (abs(indyVal - indyVal) > 2) {
    static unsigned long ind_time = 0;
    if ((now - ind_time) > analog_filter) {
      ind_time = now;
    }
  }
  dynVal = analogRead(dynamicBrakePin);
  if (abs(dynVal - dynVal) > 2) {
    static unsigned long dyn_time = 0;
    if ((now - dyn_time) > analog_filter) {
      dyn_time = now;
    }
  }
  thrVal = analogRead(throttlePin);
  if (abs(thrVal - thrVal) > 2) {
    static unsigned long thr_time = 0;
    if ((now - thr_time) > analog_filter) {
      thr_time = now;
    }
  }
  revVal = analogRead(reverserPin);
  if (abs(revVal - revVal) > 2) {
    static unsigned long rev_time = 0;
    if ((now - rev_time) > analog_filter) {
      rev_time = now;
    }
  }

  // --- Digital/Analog buttons (debounced) ---
  bool sand_cur = !digitalRead(sandPin);
  if (sand_cur != sand_prev && (now - sand_last) > debounce_filter) {
    sandVal = sand_cur;
    sand_prev = sand_cur;
    sand_last = now;
  }
  bool bell_cur = analogRead(bellPin) > 200;
  if (bell_cur != bell_prev && (now - bell_last) > debounce_filter) {
    bellVal = !bell_cur;
    bell_prev = bell_cur;
    bell_last = now;
  }
  bool alerter_cur = analogRead(alerterPin) > 200;
  if (alerter_cur != alerter_prev && (now - alerter_last) > debounce_filter) {
    alerterVal = !alerter_cur;
    alerter_prev = alerter_cur;
    alerter_last = now;
  }
  bool horn_cur = !digitalRead(hornPin);
  if (horn_cur != horn_prev && (now - horn_last) > debounce_filter) {
    hornVal = horn_cur;
    horn_prev = horn_cur;
    horn_last = now;
  }

  // --- Muxed momentaries (debounced) ---
  // Counter (up=1, down=2, idle=0)
  int counter_cur = 0;
  if (!readMux(8))       counter_cur = 1;
  else if (!readMux(7))  counter_cur = 2;
  if (counter_cur != prev_counter_pos && (now - mux_debounce[8]) > debounce_filter) {
    counterPos = counter_cur;
    prev_counter_pos = counter_cur;
    mux_debounce[8] = now;
  }

  // DPU Fence (left=1, right=2, idle=0)
  int dpuFence_cur = 0;
  if (!readMux(13))      dpuFence_cur = 2;
  else if (!readMux(14)) dpuFence_cur = 1;
  if (dpuFence_cur != prev_dpuFencePos && (now - mux_debounce[13]) > debounce_filter) {
    dpuFencePos = dpuFence_cur;
    prev_dpuFencePos = dpuFence_cur;
    mux_debounce[13] = now;
  }

  // DPU Throttle (left=1, right=2, idle=0)
  int dpuThr_cur = 0;
  if (!readMux(11))      dpuThr_cur = 1;
  else if (!readMux(12)) dpuThr_cur = 2;
  if (dpuThr_cur != prev_dpuThrPos && (now - mux_debounce[11]) > debounce_filter) {
    dpuThrPos = dpuThr_cur;
    prev_dpuThrPos = dpuThr_cur;
    mux_debounce[11] = now;
  }

  // Single-channel momentaries
  bool dpuDyn_cur      = !readMux(9);
  bool slowSpeed_cur   = !readMux(10);
  bool handbrake_cur   = !readMux(6);
  bool wiper_cur       = !readMux(5);
  bool lightGauge_cur  = !readMux(4);
  bool lightCab_cur    = !readMux(3);
  bool engineRun_cur   = !readMux(2);
  bool genField_cur    = !readMux(1);
  bool control_cur     = !readMux(0);
  bool bail_cur        = readMux(15);

  if (dpuDyn_cur != prev_dpuDynVal && (now - mux_debounce[9]) > debounce_filter) {
    dpuDynVal = dpuDyn_cur;
    prev_dpuDynVal = dpuDyn_cur;
    mux_debounce[9] = now;
  }
  if (slowSpeed_cur != prev_slowSpeedVal && (now - mux_debounce[10]) > debounce_filter) {
    slowSpeedVal = slowSpeed_cur;
    prev_slowSpeedVal = slowSpeed_cur;
    mux_debounce[10] = now;
  }
  if (handbrake_cur != prev_handbrakeVal && (now - mux_debounce[6]) > debounce_filter) {
    handbrakeVal = handbrake_cur;
    prev_handbrakeVal = handbrake_cur;
    mux_debounce[6] = now;
  }
  if (wiper_cur != prev_wiperVal && (now - mux_debounce[5]) > debounce_filter) {
    wiperVal = wiper_cur;
    prev_wiperVal = wiper_cur;
    mux_debounce[5] = now;
  }
  if (lightGauge_cur != prev_lightGaugeVal && (now - mux_debounce[4]) > debounce_filter) {
    lightGaugeVal = lightGauge_cur;
    prev_lightGaugeVal = lightGauge_cur;
    mux_debounce[4] = now;
  }
  if (lightCab_cur != prev_lightCabVal && (now - mux_debounce[3]) > debounce_filter) {
    lightCabVal = lightCab_cur;
    prev_lightCabVal = lightCab_cur;
    mux_debounce[3] = now;
  }
  if (engineRun_cur != prev_engineRunVal && (now - mux_debounce[2]) > debounce_filter) {
    engineRunVal = engineRun_cur;
    prev_engineRunVal = engineRun_cur;
    mux_debounce[2] = now;
  }
  if (genField_cur != prev_genFieldVal && (now - mux_debounce[1]) > debounce_filter) {
    genFieldVal = genField_cur;
    prev_genFieldVal = genField_cur;
    mux_debounce[1] = now;
  }
  if (control_cur != prev_controlVal && (now - mux_debounce[0]) > debounce_filter) {
    controlVal = control_cur;
    prev_controlVal = control_cur;
    mux_debounce[0] = now;
  }
  if (bail_cur != prev_bailVal && (now - mux_debounce[15]) > debounce_filter) {
    bailVal = bail_cur;
    prev_bailVal = bail_cur;
    mux_debounce[15] = now;
  }

  // --- Headlight rotary switches (debounced) ---
  int frontHeadlight_cur = 0;
  if (!digitalRead(frontOffPin))      frontHeadlight_cur = 0;
  else if (!digitalRead(frontLowPin)) frontHeadlight_cur = 1;
  else if (!digitalRead(frontHighPin))frontHeadlight_cur = 2;
  if (frontHeadlight_cur != prev_frontHeadlightPos && (now - frontHeadlight_last) > debounce_filter) {
    frontHeadlightPos = frontHeadlight_cur;
    prev_frontHeadlightPos = frontHeadlight_cur;
    frontHeadlight_last = now;
  }

  int rearHeadlight_cur = 0;
  if (!digitalRead(rearOffPin))       rearHeadlight_cur = 0;
  else if (!digitalRead(rearLowPin))  rearHeadlight_cur = 1;
  else if (!digitalRead(rearHighPin)) rearHeadlight_cur = 2;
  if (rearHeadlight_cur != prev_rearHeadlightPos && (now - rearHeadlight_last) > debounce_filter) {
    rearHeadlightPos = rearHeadlight_cur;
    prev_rearHeadlightPos = rearHeadlight_cur;
    rearHeadlight_last = now;
  }

  // --- Serial output (unchanged) ---
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
      Serial.println(rearHeadlightPos);
    }
    else if (incomingByte == 'I') { // Identification request
      Serial.print("miniRD,");
      Serial.println(version);
    }
  }
}
