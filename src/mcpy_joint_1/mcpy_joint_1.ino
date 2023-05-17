// MoCopy joint device (between external and central).
// Install the ArduinoBLE library from the library manager in the IDE.

#include <ArduinoBLE.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <mocopy.h>

using namespace mocopy;

// --------------------------- Hardware defines --------------------

// Motors for joint device
#define UP_MOTOR        D9
#define DOWN_MOTOR      D8
#define LEFT_MOTOR      D7
#define RIGHT_MOTOR     D6

// ----- Global Variables -----
Adafruit_BNO055 bno;
imu::Vector<3> joint_vector, external_vector, error_vector, correct_vector, calibrate_vector;
bool reset_bno_joint, external_wiggles, joint_wiggles;
float joint_pitch, external_pitch;
char print_string[64];
// buffer for sending and recieving float data over BLE
byte buf[12] = {0};

// BLE variables
BLEDevice central, external;
BLEService joint_service(JOINT_SERVICE_UUID);
BLEFloatCharacteristic pitch_diff_characteristic(PITCH_DIFF_CHARACTERISTIC_UUID, BLERead);
BLEBoolCharacteristic reset_bno_joint_characteristic(RESET_BNO_JOINT_CHARACTERISTIC_UUID, BLERead | BLEWrite);
BLEBoolCharacteristic both_wiggles_characteristic(BOTH_WIGGLES_CHARACTERISTIC_UUID, BLERead);
BLEService external_service(EXTERNAL_SERVICE_UUID); // from external
BLECharacteristic reset_bno_external_characteristic; // from external

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
// initialize the BLE
void initBLE() {
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while(1);
  }
  Serial.println("Bluetooth® Low Energy module initialized.");

  // Construct the service to be advertised.
  joint_service.addCharacteristic(pitch_diff_characteristic);
  joint_service.addCharacteristic(reset_bno_joint_characteristic);
  joint_service.addCharacteristic(both_wiggles_characteristic);
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
    BLECharacteristic reset_bno_external_characteristic = external.characteristic(RESET_BNO_EXTERNAL_CHARACTERISTIC_UUID);
    BLECharacteristic joint_orientation = external.characteristic(JOINT_ORIENTATION_CHARACTERISTIC_UUID);
    BLECharacteristic external_orientation = external.characteristic(EXTERNAL_ORIENTATION_CHARACTERISTIC_UUID);
    BLECharacteristic external_wiggles_characteristic = external.characteristic(EXTERNAL_WIGGLES_CHARACTERISTIC_UUID);

    if (!external_orientation || !external_wiggles_characteristic) {
      Serial.println("External device does not have the expected characteristic(s).");
      external.disconnect();
      return;
    } else if (!external_orientation.canSubscribe()) { // joint_orientation is BLEWrite threfore can't subscribe.
      Serial.println("Cannot subscribe to the External device's characteristic(s).");
      external.disconnect();
      return;
    }
    
    while (central.connected() && external.connected()) {
      external_wiggles_characteristic.readValue(&external_wiggles, 1);
      if (external_wiggles && joint_wiggles) {
        both_wiggles_characteristic.writeValue(joint_wiggles);
      }
      if (reset_bno_joint_characteristic.written()){
        reset_bno_joint_characteristic.readValue(&reset_bno_joint, 1);
        if (reset_bno_joint){
          buf[0] = true;
          reset_bno_external_characteristic.writeValue(buf[0], 1);
          calibrate_vector = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
        }
      }
      updateHardware();
      memcpy(buf, &joint_pitch, 4);
      joint_orientation.setValue(buf, 4);
      external_orientation.readValue(buf, 4);
      memcpy(&external_pitch, buf, 4);
      float diff = fabs(joint_pitch - external_pitch);
      pitch_diff_characteristic.setValue(diff);
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
  joint_vector = joint_vector - calibrate_vector;
  joint_pitch = joint_vector[2];
  error_vector = joint_vector - correct_vector;

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

void setup() {
  correct_vector = {0, 0, 0};
  initSerial();
  initHardware(bno, UP_MOTOR, DOWN_MOTOR, LEFT_MOTOR, RIGHT_MOTOR);
  reset_bno_joint = false;
  calibrateBNO(bno, calibrate_vector);
  joint_wiggles = true;
  initBLE();
}

void loop() {
  updateBLE();
}
