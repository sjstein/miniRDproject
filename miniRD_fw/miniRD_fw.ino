#include <Arduino.h>

// "Firmware" version info
const String version = "0225a";

// Analog break-point values for three-way switch inputs
// The three way switches (reverse, counter) use a voltage divider to signal their positions.
// The values below define how we determine the switch positions. 
const int low_bp = 250;
const int high_bp = 750;

// Hardware pin locations
// NOTE: The actual translation of these inputs to the UDP stream sent to the game client is defined in the 
//   miniRD server code.
// These are specific to the model of Arduino/ESP board being used
int autoBrakePin = A0;
int indyBrakePin = A1;
int dynamicBrakePin = A2;
int throttlePin = A3;
int reverserPin = A4;
int counterPin = A5;
// generic "pb" names are used here, but the original intended functions are shown in comments
int pb0Pin = 0;   // Bail 
int pb1Pin = 1;   // Horn
int pb2Pin = 5;   // Bell
int pb3Pin = 7;   // Front headlight
int pb4Pin = 9;   // Rear headlight
int pb5Pin = 10;  // Wiper toggle
int pb6Pin = 11;  // Sander
int pb7Pin = 12;  // Alerter

// Last known update value for analog inputs
int prev_auto_val = 0;
int prev_indy_val = 0;
int prev_dynamic_val = 0;
int prev_throttle_val = 0;

// Current value for analog inputs
int auto_val = 0;
int indy_val = 0;
int dynamic_val = 0;
int throttle_val = 0;
int reverser_val = 0;
int counter_val = 0;

// Enumerate reverser and counter (analog input) to integers
int reverser_pos, prev_reverser_pos = 1;  // 0 = reverse, 2 = neutral, 1 = fwd
int counter_pos, prev_counter_pos = 0;    // 2 = count down, 0 = center, 1 = count up (corresponds to Run8 API)

// pushbutton values (current and last)
bool pb0_val, pb0_lval = false;
bool pb1_val, pb1_lval = false;
bool pb2_val, pb2_lval = false;
bool pb3_val, pb3_lval = false;
bool pb4_val, pb4_lval = false;
bool pb5_val, pb5_lval = false;
bool pb6_val, pb6_lval = false;
bool pb7_val, pb7_lval = false;
unsigned long debounce_filter = 50; // Filter time in ms
unsigned long analog_filter = 50;
unsigned long pb0_time, pb1_time, pb2_time, pb3_time, pb4_time, pb5_time, pb6_time, pb7_time = 0;
unsigned long aut_time, ind_time, dyn_time, thr_time, rev_time, ctr_time = 0;

int incomingByte = 0; // for incoming serial data

void setup()
{
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    pinMode(pb0Pin, INPUT_PULLUP);  // Bail
    pinMode(pb1Pin, INPUT_PULLUP);  // Horn
    pinMode(pb2Pin, INPUT_PULLUP);  // Bell
    pinMode(pb3Pin, INPUT_PULLUP);  // Front headlight
    pinMode(pb4Pin, INPUT_PULLUP);  // Rear headlight
    pinMode(pb5Pin, INPUT_PULLUP);  // Wiper
    pinMode(pb6Pin, INPUT_PULLUP);  // Sander
    pinMode(pb7Pin, INPUT_PULLUP);  // Alerter
}

String scale(int input){
  int scaled_val = int(float(input-5)/1000 * 255);
  if (scaled_val > 255) { 
    scaled_val = 255;
    }
  if (scaled_val < 0) {
    scaled_val = 0;
  }
  return String(scaled_val);
}

void loop()
{
  // Read analog channels, output scaled values between (0,255)
  auto_val = analogRead(autoBrakePin);
  if (auto_val != prev_auto_val) {
    if ((millis() - aut_time) > analog_filter){ // reduce noise
      prev_auto_val = auto_val;
      aut_time = millis();
    }
  }

  indy_val = analogRead(indyBrakePin);
    if (indy_val != prev_indy_val) {
    if ((millis() - ind_time) > analog_filter){ // reduce noise
      prev_indy_val = indy_val;
      ind_time = millis();
    }
  }

  dynamic_val = analogRead(dynamicBrakePin);
    if (dynamic_val != prev_dynamic_val) {
    if ((millis() - dyn_time) > analog_filter){ // reduce noise
      prev_dynamic_val = dynamic_val;
      dyn_time = millis();
    }
  }

  throttle_val = analogRead(throttlePin);
    if (throttle_val != prev_throttle_val) {
    if ((millis() - thr_time) > analog_filter){ // reduce noise
      prev_throttle_val = throttle_val;
      thr_time = millis();
    }
  }

  // Read toggle switches via analog inputs and enumerate to ints
  reverser_val = analogRead(reverserPin);
    if (reverser_val < low_bp) {
      reverser_pos = 0;
    } else if (reverser_val > high_bp) {
      reverser_pos = 2;
    } else {
      reverser_pos = 1;
    }
    if (reverser_pos != prev_reverser_pos) {
    if ((millis() - rev_time) > analog_filter){ // reduce noise
      prev_reverser_pos = reverser_pos;
      rev_time = millis();
    }
  }

  counter_val = analogRead(counterPin);
    if (counter_val < low_bp) {
      counter_pos = 2;
    } else if (counter_val > high_bp) {
      counter_pos = 0;
    } else {
      counter_pos = 1;
    }
    if (counter_pos != prev_counter_pos) {
    if ((millis() - ctr_time) > analog_filter){ // reduce noise
      prev_counter_pos = counter_pos;
      ctr_time = millis();
    }
  }

  // Read digital channels
  pb0_val = digitalRead(pb0Pin);
  if (pb0_val != pb0_lval){
    if ((millis() - pb0_time) > debounce_filter){ // debounce
      pb0_lval = pb0_val;
      pb0_time = millis();
    }
  }

  pb1_val = digitalRead(pb1Pin);
  if (pb1_val != pb1_lval){
    if ((millis() - pb1_time) > debounce_filter){ // debounce
      pb1_lval = pb1_val;
      pb1_time = millis();
    }
  }

  pb2_val = digitalRead(pb2Pin);
  if (pb2_val != pb2_lval){
    if ((millis() - pb2_time) > debounce_filter){ // debounce
      pb2_lval = pb2_val;
      pb2_time = millis();
    }
  }

  pb3_val = digitalRead(pb3Pin);
  if (pb3_val != pb3_lval){
    if ((millis() - pb3_time) > debounce_filter){ // debounce
      pb3_lval = pb3_val;
      pb3_time = millis();
    }
  }

  pb4_val = digitalRead(pb4Pin);
  if (pb4_val != pb4_lval){
    if ((millis() - pb4_time) > debounce_filter){ // debounce
      pb4_lval = pb4_val;
      pb4_time = millis();
    }
  }

  pb5_val = digitalRead(pb5Pin);
  if (pb5_val != pb5_lval){
    if ((millis() - pb5_time) > debounce_filter){ // debounce
      pb5_lval = pb5_val;
      pb5_time = millis();
    }
  }

  pb6_val = digitalRead(pb6Pin);
  if (pb6_val != pb6_lval){
    if ((millis() - pb6_time) > debounce_filter){ // debounce
      pb6_lval = pb6_val;
      pb6_time = millis();
    }
  }

  pb7_val = digitalRead(pb7Pin);
  if (pb7_val != pb7_lval){
    if ((millis() - pb7_time) > debounce_filter){ // debounce
      pb7_lval = pb7_val;
      pb7_time = millis();
    }
  }

  // Check to see if a status update request has been initiated from the host (daemon).
  // Return status of all inputs as a comma delimited string in the following order:
  // <auto_brake>, <indy_brake>, <dynamic_brake>, <throttle>, <reverser>, <counter>, 
  // <bail_off>, <horn>, <bell>, <front_light>, <rear_light>, <wiper>, <sand>, <alerter>

  if (Serial.available() > 0) {
    incomingByte = Serial.read();
      if (incomingByte == 'r') {  // read request
        Serial.print(auto_val);
        Serial.print(",");
        Serial.print(indy_val);
        Serial.print(",");
        Serial.print(dynamic_val);
        Serial.print(",");
        Serial.print(throttle_val);
        Serial.print(",");
        Serial.print(reverser_pos);
        Serial.print(",");
        Serial.print(counter_pos);
        Serial.print(",");
        Serial.print(not(pb0_lval));
        Serial.print(",");
        Serial.print(not(pb1_lval));
        Serial.print(",");
        Serial.print(not(pb2_lval));
        Serial.print(",");
        Serial.print(not(pb3_lval));
        Serial.print(",");
        Serial.print(not(pb4_lval));
        Serial.print(",");
        Serial.print(not(pb5_lval));
        Serial.print(",");
        Serial.print(not(pb6_lval));
        Serial.print(",");
        Serial.println(not(pb7_lval));
        } 
      else if (incomingByte == 'I') { // Identification request
        Serial.print("miniRD,");
        Serial.println(version);
      }
    }
}