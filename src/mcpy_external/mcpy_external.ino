// An external device for the MoCopy® system, i.e. the outer most band
// on a particular limb such as the wrist on the arm.
// Install the ArduinoBLE library from the library manager in the IDE.

#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <ArduinoBLE.h>
#include <mocopy.h>

using namespace mocopy;

// Motors for external device
#define UP_MOTOR        D7
#define DOWN_MOTOR      D9
#define LEFT_MOTOR      D6
#define RIGHT_MOTOR     D8

BLEService externalService(EXTERNAL_SERVICE_UUID);
// BLECharacteristic joint_orientation(JOINT_ORIENTATION_CHARACTERISTIC_UUID, BLEWrite, 12);
BLECharacteristic external_orientation(EXTERNAL_ORIENTATION_CHARACTERISTIC_UUID, BLERead | BLENotify, 12);
BLEBoolCharacteristic reset_BNO(RESET_BNO_EXTERNAL_CHARACTERISTIC_UUID, BLEWrite);
BLEBoolCharacteristic external_wiggles_characteristic(EXTERNAL_WIGGLES_CHARACTERISTIC_UUID, BLERead);
BLECharacteristic key_frame_data_characteristic(EXTERNAL_KEY_FRAME_DATA_CHARACTERISTIC_UUID, BLEWrite, 24);
BLEDevice joint;

Adafruit_BNO055 bno;
imu::Vector<3> joint_euler_vector, external_euler_vector, correct_vector, error_vector, calibrate_vector;
imu::Vector<3> correct_kf_vector, correct_joint_vector, correct_external_vector;
// float joint_pitch, external_pitch, joint_yaw, external_yaw, joint_roll, external_roll;
// float joint_pitch, joint_yaw, joint_roll;
float correct_joint_pitch, correct_joint_yaw, correct_joint_roll;
float external_pitch, external_yaw, external_roll;
float kf_pitch, kf_yaw, kf_roll;
bool bno_reset, external_wiggles;
char printString [64];
byte buf[12] = {0};

uint8_t vibes[3] = {0, 0, 0};

void initBLE() {
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while(1);
  }
  Serial.println("Bluetooth® Low Energy module initialized.");

  // Construct the service to be advertised.
  // externalService.addCharacteristic(joint_orientation);
  externalService.addCharacteristic(external_orientation);
  externalService.addCharacteristic(reset_BNO);
  externalService.addCharacteristic(external_wiggles_characteristic);
  externalService.addCharacteristic(key_frame_data_characteristic);
  BLE.addService(externalService);

  // Setup peripheral advertising.
  BLE.setLocalName("MoCopy External");
  BLE.setAdvertisedService(externalService);
  BLE.advertise();
  Serial.print("Advertising with address: ");
  Serial.println(BLE.address().c_str());
}

void updateHardware() {
  external_euler_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  external_euler_vector = external_euler_vector - calibrate_vector;
  external_yaw = external_euler_vector[0];
  external_roll = external_euler_vector[1];
  external_pitch = external_euler_vector[2];

  error_vector = external_euler_vector - correct_external_vector;

  if (DEBUG_PRINT_VECTORS) {
    char vector_str[128];
    sprintf(vector_str, "eV: <%f, %f, %f>", 
      error_vector[0],
      error_vector[1],
      error_vector[2]
    );

    Serial.println(vector_str);
  }

  // Calculate the vibration strength
  vibes[0] = max(0.0, min((uint8_t)(fabs(error_vector[0])) - GRACE_ANGLE_DEGREES + BASE_LED, MAX_LED));
  vibes[2] = max(0.0, min((uint8_t)(2 * fabs(error_vector[2])) - GRACE_ANGLE_DEGREES + BASE_LED, MAX_LED));

  if (error_vector[0] >= GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Right, ");
    analogWrite(RIGHT_MOTOR, vibes[0]);
    analogWrite(LEFT_MOTOR, 0);
  } else if (error_vector[0] <= -GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Left, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, vibes[0]);
  } else {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Grace, ");
    analogWrite(RIGHT_MOTOR, 0);
    analogWrite(LEFT_MOTOR, 0);
  }

  if (error_vector[2] >= GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Down, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, vibes[2]);
  } else if (error_vector[2] <= -GRACE_ANGLE_DEGREES) {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Up, ");
    analogWrite(UP_MOTOR, vibes[2]);
    analogWrite(DOWN_MOTOR, 0);
  } else {
    if (DEBUG_PRINT_DIRECTION) Serial.print("Grace, ");
    analogWrite(UP_MOTOR, 0);
    analogWrite(DOWN_MOTOR, 0);
  } 

  delay(SAMPLE_PERIOD_MS);
}

void updateBLE() {
  joint = BLE.central();
  // Note: The peripheral does not attempt a connection to the joint and thus
  // does not call the connect() method.

  if (joint) {
    Serial.print("Connected to joint MAC: ");
    Serial.println(joint.address());

    while (joint.connected()) {
      updateHardware();
      // joint_orientation.readValue(buf, 12);
      // memcpy(&joint_yaw, &buf[0], 4);
      // memcpy(&joint_roll, &buf[4], 4);
      // memcpy(&joint_pitch, &buf[8], 4);
      key_frame_data_characteristic.readValue(buf, 24);
      // taking the data from the key frame buffer into device for parsing
      memcpy(&correct_joint_yaw, &buf[0], 4);
      memcpy(&correct_joint_roll, &buf[4], 4);
      memcpy(&correct_joint_pitch, &buf[8], 4);
      memcpy(&kf_yaw, &buf[12], 4);
      memcpy(&kf_roll, &buf[16], 4);
      memcpy(&kf_pitch, &buf[20], 4);

      // moving all the key frame data into vectors for ez math
      correct_joint_vector[0] = correct_joint_yaw;
      correct_joint_vector[1] = correct_joint_roll;
      correct_joint_vector[2] = correct_joint_pitch;

      correct_kf_vector[0] = correct_kf_yaw;
      correct_kf_vector[1] = correct_kf_roll;
      correct_kf_vector[2] = correct_kf_pitch;

      correct_external_vector = correct_joint_vector + correct_kf_vector;

      memcpy(&buf[0], &external_euler_vector[0], 4);
      memcpy(&buf[4], &external_euler_vector[1], 4);
      memcpy(&buf[8], &external_euler_vector[2], 4);
      external_orientation.setValue(buf, 12);

      if (reset_BNO.written()) {
        reset_BNO.readValue(&bno_reset, 1);
        if (bno_reset) {
          calibrate_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
        }
      }
    }

    Serial.println("Disconnected from joint MAC: ");
    Serial.println(joint.address());
  }
}

void setup() {
  initSerial();
  initHardware(bno, UP_MOTOR, DOWN_MOTOR, LEFT_MOTOR, RIGHT_MOTOR);
  calibrateBNO(bno, calibrate_vector);
  external_wiggles_characteristic.writeValue(true);
  bno_reset = false;
  correct_external_vector = {0, 0, 0};
  initBLE();
}

void loop() {
  updateBLE();
}
