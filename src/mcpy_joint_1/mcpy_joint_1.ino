// MoCopy joint device (between external and central).
// Install the ArduinoBLE library from the library manager in the IDE.

#include <string>
#include <math.h>
#include <ArduinoBLE.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>

// --------------------------- BLE defines -------------------------

#define JOINT_SERVICE_UUID "a9a95e92-26ea-4282-bd0c-7c8bd6c65a2b"
#define EXTERNAL_SERVICE_UUID "56176a63-d563-43f4-b239-636f41b63c6d"

// Characteristic UUIDs
// local uuids
#define REP_COMPLETION_CHARACTERISTIC_UUID "08d54caf-75bc-4aa6-876b-8eea5427605a"

// external uuids
#define JOINT_ORITENTATION_CHARACTERISTIC_UUID "b99cc0f3-8cdc-4bb1-a51d-3927431f0985"
#define EXTERNAL_ORIENTATION_CHARACTERISTIC_UUID "04308b2c-90dc-4984-8c45-81650dff60b8"

// --------------------------- BLE defines -------------------------

// --------------------------- Hardware defines --------------------

// RGB LED
#define RED             D2
#define GREEN           D3
#define BLUE            D4

// Motors
#define UP_MOTOR        D9
#define DOWN_MOTOR      D8
#define LEFT_MOTOR      D7
#define RIGHT_MOTOR     D6

// --------------------------- Hardware defines --------------------

// ------ Math defines --------

#define SAMPLE_PERIOD_MS 100
#define YAW_GRACE_ANGLE_DEGREES 15
#define PITCH_GRACE_ANGLE_DEGREES 15
#define MAX_VIBRATION 255
#define BASE_VIBRATION 120 // determined experimentally
#define SAMPLE_PERIOD_MS 10

// ------ Math defines --------

// ------ Other defines -------

#define STR_SIZE 64

// debug defines
#define DEBUG_PRINT_JOINT 0
#define DEBUG_PRINT_BLE 1
#define DEBUG_PRINT_STATUS 0

// ------ Other defines -------

// ---- Hardware functions ----

uint8_t min(uint8_t a, uint8_t b) {if (a <= b) return a; else return b; }
uint8_t max(uint8_t a, uint8_t b) {if (a >= b) return a; else return b; }

// ---- Hardware functions ----


// ----- Global Variables -----

Adafruit_BNO055 bno;
imu::Vector<3> joint_vector, external_vector, error_vector, correct_vector;

float joint_pitch, external_pitch;
char print_string[STR_SIZE];
// buffer for sending and recieving float data over BLE
byte buf[4] = {0};

// BLE variables
BLEDevice central, external;
BLEService joint_service(JOINT_SERVICE_UUID);
BLEService external_service(EXTERNAL_SERVICE_UUID);
BLEFloatCharacteristic rep_completion_characteristic(REP_COMPLETION_CHARACTERISTIC_UUID, BLERead | BLENotify);

// Hardware variables
/*
                      (
                     (  ) (
                      )    )
         |||||||     (  ( (
        ( O   O )        )
 ____oOO___(_)___OOo____(
(_______________________)
*/
uint8_t vibes[3] = {0, 0, 0};

// ----- Global Variables -----

// initialize the BNO055 and all the pins
void initHardware() {
  // start the bno
  if (!bno.begin(OPERATION_MODE_NDOF)) {
    Serial.println("\nFailed to find BNO055 chip");
    while (1);
  }

  Serial.println("\nBNO055 Found!");
  bno.enterNormalMode();

  // init rgb pins
  pinMode(RED,          OUTPUT);
  pinMode(GREEN,        OUTPUT);
  pinMode(BLUE,         OUTPUT);
  digitalWrite(RED,     LOW);
  digitalWrite(GREEN,   LOW);
  digitalWrite(BLUE,    LOW);

  // init motor pins
  analogWriteResolution(8);
  pinMode(UP_MOTOR,     OUTPUT);
  pinMode(DOWN_MOTOR,   OUTPUT);
  pinMode(LEFT_MOTOR,   OUTPUT);
  pinMode(RIGHT_MOTOR,  OUTPUT);

  Serial.println("Mcpy hardware initialized");

  correct_vector = {0, 0, 0};
}

// initialize the BLE
void initBLE() {
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while(1);
  }
  Serial.println("Bluetooth® Low Energy module initialized.");

  // Construct the service to be advertised.
  joint_service.addCharacteristic(rep_completion_characteristic);
  BLE.addService(joint_service);

  // Setup external advertising.
  BLE.setLocalName("MoCopy (joint)");
  BLE.setAdvertisedService(joint_service);
  BLE.advertise();

  Serial.print("Advertising with address: ");
  Serial.println(BLE.address().c_str());

  // Scan for external services.
  Serial.println("Scanning for External service");
  do {
    BLE.scanForUuid(EXTERNAL_SERVICE_UUID);
    external = BLE.available();
  } while (!external);

  Serial.println("external device found.");
  Serial.print("Device MAC address: ");
  Serial.println(external.address());
  Serial.print("Device Name: ");
  Serial.println(external.localName());

  // Print all services advertised by the external.
  if (external.hasAdvertisedServiceUuid()) {
    Serial.println("External's advertised services UUIDs:");
    for (int i = 0; i < external.advertisedServiceUuidCount(); i++) {
      Serial.print(external.advertisedServiceUuid(i));
      Serial.print(", ");
    }
    Serial.println();
  } else {
    Serial.println("External has no advertised services!");
  }

  BLE.stopScan();
}

void setup() {
  Serial.begin(9600);
  delay(1000); // wait for Serial

  initHardware();
  initBLE();
}

void updateBLE() {
  central = BLE.central();

  if (central) {
    Serial.print("Connected to central MAC: ");
    Serial.println(central.address());

    // Maybe change this to a for loop to make X attempts before quitting.
    if (!external.connected()) {
      if (external.connect()) {
        Serial.println("Successfully connected to External.");
      } else {
        Serial.println("Failed to connect to External.");
        Serial.print("Device MAC address: ");
        Serial.println(external.address());
        Serial.print("Device Name: ");
        Serial.println(external.localName());
        return;
      }
    }

    if (external.discoverAttributes()) {
      Serial.println("Successfully discovered External attributes.");
    } else {
      Serial.println("Failed to discover External attributes.");
      external.disconnect();
      return;
    }

    BLECharacteristic joint_orientation = external.characteristic(JOINT_ORITENTATION_CHARACTERISTIC_UUID);
    BLECharacteristic external_orientation = external.characteristic(EXTERNAL_ORIENTATION_CHARACTERISTIC_UUID);

    if (!joint_orientation || !external_orientation) {
      Serial.println("External device does not have the expected characteristic(s).");
      external.disconnect();
      return;
    } else if (!external_orientation.canSubscribe()) { // joint_orientation is BLEWrite threfore can't subscribe.
      Serial.println("Cannot subscribe to the External device's characteristic(s).");
      external.disconnect();
      return;
    }

    while (central.connected() && external.connected()) {
      updateHardware();
      memcpy(buf, &joint_pitch, 4);
      joint_orientation.setValue(buf, 4);

      external_orientation.readValue(buf, 4);
      memcpy(&external_pitch, buf, 4);

      float diff = fabs(joint_pitch - external_pitch);

      if (diff < 0.2) {
        rep_completion_characteristic.setValue(1);
      } else {
        rep_completion_characteristic.setValue(0);
      }

      if (DEBUG_PRINT_BLE) {
        Serial.print("Joint pitch: ");
        Serial.print(joint_pitch);
        Serial.print(" External pitch: ");
        Serial.println(external_pitch);
      }
    }

    if (!central.connected()) {
      Serial.print("Disconnected from central MAC: ");
      Serial.println(central.address());
    }

    if (!external.connected()) {
      Serial.print("Disconnected from External MAC: ");
      Serial.println(external.address());
    }
  }
}

void updateHardware() {
  joint_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  joint_pitch = joint_vector[2];
  if (DEBUG_PRINT_JOINT) {
    sprintf(print_string,
      "EuV <%f, %f, %f>",
      joint_vector[0],
      joint_vector[1],
      joint_vector[2]
    );
    Serial.println(print_string);
  }

  error_vector = joint_vector - correct_vector;

  // Calculate the vibration strength
  vibes[0] = max(0.0, min((uint8_t)(fabs(error_vector[0])) - YAW_GRACE_ANGLE_DEGREES + BASE_VIBRATION, MAX_VIBRATION));
  vibes[2] = max(0.0, min((uint8_t)(2 * fabs(error_vector[2])) - PITCH_GRACE_ANGLE_DEGREES + BASE_VIBRATION, MAX_VIBRATION));

  digitalWrite(RED, LOW);
  digitalWrite(GREEN, LOW);
  digitalWrite(BLUE, LOW);

  if (error_vector[0] >= YAW_GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_STATUS) Serial.print("Right, ");
    analogWrite(RIGHT_MOTOR, vibes[0]);
    analogWrite(LEFT_MOTOR, 0);
    digitalWrite(RED, HIGH);
  } else if (error_vector[0] <= -YAW_GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_STATUS) Serial.print("Left, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, vibes[0]);
    digitalWrite(GREEN, HIGH);
  } else {
    if (DEBUG_PRINT_STATUS) Serial.print("Grace, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, 0);
    digitalWrite(BLUE, HIGH);
  }

  if (error_vector[2] >= PITCH_GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_STATUS) Serial.print("Down, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, vibes[2]);
    digitalWrite(RED, HIGH);
    digitalWrite(GREEN, HIGH);
  } else if (error_vector[2] <= -PITCH_GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_STATUS) Serial.print("Up, ");
    analogWrite(UP_MOTOR, vibes[2]);
    analogWrite(DOWN_MOTOR, 0);
    digitalWrite(RED, HIGH);
    digitalWrite(BLUE, HIGH);
  } else {
    if (DEBUG_PRINT_STATUS) Serial.print("Grace, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, 0);
    digitalWrite(GREEN, HIGH);
    digitalWrite(BLUE, HIGH);
  } 

  delay(SAMPLE_PERIOD_MS);
}

void loop() {
  updateBLE();
}