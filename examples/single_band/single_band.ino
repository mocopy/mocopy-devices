// This sketch demonstrates error feedback on a single band
// and assumes LEDs are used instead of vibration motors.
// No Bluetooth connectivity or other devices are required
// for this sketch to work.

#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>

// ===== Developer definitions =====
#define FORCE_SERIAL 1
#define DEBUG_PRINT_DIRECTION 1

// ===== Hardware definitions =====
// FOR THE RED AND BLUE BAND
// #define UP_MOTOR        D9
// #define DOWN_MOTOR      D7
// #define LEFT_MOTOR      D6
// #define RIGHT_MOTOR     D8

// FOR THE BLUE AND YELLOW BAND
#define UP_MOTOR        D9
#define DOWN_MOTOR      D6
#define LEFT_MOTOR      D8
#define RIGHT_MOTOR     D7

#define MAX_LED 255
#define BASE_LED 255

// ===== Other definitions =====
#define SAMPLE_PERIOD_MS 10
#define GRACE_ANGLE_DEGREES 15

//  ===== Global variables =====
char print_string[64];
Adafruit_BNO055 bno;
imu::Vector<3> correct_vector, error_vector, raw_vector, calibrate_vector, curr_vector;
// correct_vector is where we should be.
// raw_vector is the pre-processed reading from the device.
// calibrate_vector is the reading at the time of calibration and
//    all processed readings are relative to this.
// curr_vector is the post-processed orientation of the current value.

uint8_t feedback[3] = {0, 0, 0};
// feedback based on error *per axis*.
// E.g. motor or LED intensity on the pitch, yaw, or roll axis.

// Given an Euler vector with unbounded dimensions in any of the
// yaw, pitch, roll axes, return a vector with the equivalent
// angles within the ranges of (0 to +360, -90 to +90, -180 to +180)
// respectively.
imu::Vector normalizeEulerVector(imu::Vector vec) {
  imu::Vector result;
  // if (vec[0] > 180.0) 

  return result;
}

void initSerial() {
  Serial.begin(9600);
  if (FORCE_SERIAL) while (!Serial);
  else delay(2000);
  Serial.println("Serial communications initialized.");
}

void initHardware() {
  if (!bno.begin(OPERATION_MODE_NDOF)) {
    Serial.println("\nFailed to find BNO055 chip");
    while (1);
  }
  Serial.println("\nBNO055 Found!");
  bno.enterNormalMode();

  analogWriteResolution(8);
  pinMode(UP_MOTOR,     OUTPUT);
  pinMode(DOWN_MOTOR,   OUTPUT);
  pinMode(LEFT_MOTOR,   OUTPUT);
  pinMode(RIGHT_MOTOR,  OUTPUT);
}

void updateHardware() {
  raw_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  curr_vector = raw_vector - calibrate_vector;
  error_vector = curr_vector - correct_vector;
  // should we normalize these vectors to be within -180 to +180, etc?
  // or does the subtraction operator already do it for us?

  // Calculate the feedback intensity in each axis.
  feedback[0] = max(0.0, min((uint8_t)(fabs(error_vector[0])) - GRACE_ANGLE_DEGREES + BASE_LED, MAX_LED));
  // feedback[1] = 0; // roll is currently unused.
  feedback[2] = max(0.0, min((uint8_t)(2 * fabs(error_vector[2])) - GRACE_ANGLE_DEGREES + BASE_LED, MAX_LED));

  if (error_vector[0] >= GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Right, ");
    analogWrite(RIGHT_MOTOR, feedback[0]);
    analogWrite(LEFT_MOTOR, 0);
  } else if (error_vector[0] <= -GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Left, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, feedback[0]);
  } else {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Grace, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, 0);
  }

  if (error_vector[2] >= GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Down, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, feedback[2]);
  } else if (error_vector[2] <= -GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Up, ");
    analogWrite(UP_MOTOR, feedback[2]);
    analogWrite(DOWN_MOTOR, 0);
  } else {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Grace, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, 0);
  }
  Serial.println();

  delay(SAMPLE_PERIOD_MS);
}

void setup() {
  // This is a POST-PROCESS vector! <0, 0, 0> means curr_vector == calibrate_vector!
  correct_vector = {0, 0, 0};
  initSerial();
  initHardware();
  Serial.println("Calibrating BNO055 in 5 seconds...");
  delay(5000);
  calibrate_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  Serial.println("Calibrated.");
}

void loop() {
  updateHardware();
}