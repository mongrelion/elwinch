#define LAMP_BLINK_DURATION 1000 // ms
#define NOMINAL 0
#define SIGNAL_CALIB_MIN 10
#define CALIBRATE_MIN 20
#define CALIB_MIN_DONE 30
#define SIGNAL_CALIB_MAX 40
#define CALIBRATE_MAX 50
#define CALIB_MAX_DONE 60
#define CALIBRATION_DONE 99

const int aiLocalPot        = A2; // Analog input pin that the potentiometer is attached to
const int aoMotorController = 9;  // Analog output pin that the ESC is attached to

const int diRemoteOperationInput = 2;
const int diLocalOperationInput  = 3;
const int diCalibButton          = 4;
const int diRemoteInputThrottle  = 7; // Digital input pin that the receiver throttle is attached to

const int doBreakRelayOff = 5; // Digital output pin that switches break relay. High turns break off
const int doCalibLampOn   = 6; // Digital output pin that indicates calibration

const int breakActivationValue = 3;
const int fiveVoltValue        = 92;
const int calibCycles          = 100;
int remoteLowValue             = 1582;
int remoteHighValue            = 2014;
long remoteCalibValue          = 2014;

int regulationValueIn  = 0; // value read from the pot
int regulationValueOut = 0; // value output to the PWM (analog out)
bool remoteModeSelected = false;
bool manualModeSelected = false;
bool calibButtonActive = false;
bool remoteCalibModeActive = false;

int calibState = 0;
unsigned long lastLampBlink = 0;
int blinkCounter = 0;

void setup() {
  // initialize serial communications at 9600 bps:
  Serial.begin(9600);
  pinMode(diLocalOperationInput, INPUT_PULLUP);
  pinMode(diRemoteOperationInput, INPUT_PULLUP);
  pinMode(diRemoteInputThrottle, INPUT_PULLUP);
  pinMode(diCalibButton, INPUT_PULLUP);
  pinMode(doBreakRelayOff, OUTPUT);
  pinMode(doCalibLampOn, OUTPUT);
}

void loop() {
  remoteModeSelected = (digitalRead(diRemoteOperationInput) == LOW);
  manualModeSelected = (digitalRead(diLocalOperationInput) == LOW);
  calibButtonActive  = (digitalRead(diCalibButton) == LOW);
  CalibRemote(remoteModeSelected, calibButtonActive);
  RunMotor(remoteModeSelected, manualModeSelected, remoteCalibModeActive);
  PrintSerial;
}

void CalibRemote(bool remoteModeActive, bool calibButtonActive) {
  int nextState = 0;
  unsigned long currentTime = millis();
  remoteCalibModeActive = (calibButtonActive and remoteModeActive);
  if (remoteCalibModeActive and calibState == 0) {
    calibState = SIGNAL_CALIB_MIN;
  } else if (not remoteModeActive) {
    calibState = NOMINAL;
  }

  switch(calibState) {

    case NOMINAL:
      // Do nothing

    case SIGNAL_CALIB_MIN:

      remoteCalibValue = 0;
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION and lastLampBlink > 0) {
        digitalWrite(doCalibLampOn, 0);
        calibState = CALIBRATE_MIN;
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        Serial.print("Leave remote throttle at idle");
        digitalWrite(doCalibLampOn, 1);
      }

    case CALIBRATE_MIN:

      for (int i = 1; i <= calibCycles; i++) {
        regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
        remoteCalibValue = remoteCalibValue + regulationValueIn;
        delay(30);
      }
      remoteLowValue = remoteCalibValue / calibCycles;
      Serial.print("remote low = ");
      Serial.print(remoteLowValue);
      calibState = CALIB_MIN_DONE;

    case CALIB_MIN_DONE:

      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION/4 and lastLampBlink > 0) {
        if (blinkCounter >= 3) {
          calibState = SIGNAL_CALIB_MAX;
          blinkCounter = 0;
        } else {
          digitalWrite(doCalibLampOn, 0);
        }
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        if (blinkCounter == 0) Serial.print("Calibration of throttle min done!");
        digitalWrite(doCalibLampOn, 1);
        blinkCounter += 1;
      }

    case SIGNAL_CALIB_MAX:

      remoteCalibValue = 0;
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION and lastLampBlink > 0) {
        digitalWrite(doCalibLampOn, 0);
        calibState = CALIBRATE_MAX;
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        Serial.print("Hold remote throttle at max");
      }

    case CALIBRATE_MAX:

      for (int i = 2; i <= calibCycles; i++) {
        regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
        remoteCalibValue = remoteCalibValue + regulationValueIn;
        delay(30);
      }
      remoteHighValue = remoteCalibValue / calibCycles;
      Serial.print("remote high = ");
      Serial.println(remoteHighValue);
      calibState = CALIB_MAX_DONE;

    case CALIB_MAX_DONE:

      currentTime = millis();
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION/4 and lastLampBlink > 0) {
        if (blinkCounter >= 3) {
          calibState = CALIBRATION_DONE;
          blinkCounter = 0;
        }
        digitalWrite(doCalibLampOn, 0);
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        if (blinkCounter == 0) Serial.print("Calibration of throttle max done!");
        digitalWrite(doCalibLampOn, 1);
        blinkCounter += 1;
      }

    case CALIBRATION_DONE:
      // Do nothing and wait for release of button

    default: // Reset values
      remoteCalibModeActive = false;
  }
}

/*
 * The decision on how to run the motor is done by reading the position of the
 * 3-way switch located on pin `diRemoteOperationInput`.
 * These are the settings per position:
 * - top:    remote
 * - middle: brake
 * - bottom: local
 */
void RunMotor(bool remoteControlActive, bool manualControlActive, bool remoteCalibModeActive) {
  /*
   * TODO: Consider refactoring this conditional block. The operations could be
   *       handled in separate functions, called from within each branch
   *       of the conditional.
   *       For example: 
   *       if (digitalRead(diRemoteOperationInput) == LOW) {
   *         handleRemoteControl()
   *       } else if (digitalRead(diLocalOperationInput) == LOW){
   *         handleLocalControl()
   *       } else {
   *         handleBrake()
   *       }
   *
   */

  // Handle remote control
  if (remoteControlActive and not remoteCalibModeActive) {
    digitalWrite(doCalibLampOn, 1); // Turn on status lamp
    regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
    regulationValueOut = map(regulationValueIn, remoteLowValue, remoteHighValue, 0, fiveVoltValue);
    if (int (regulationValueOut ) <= breakActivationValue) {
      digitalWrite(doBreakRelayOff, 0);
    } else {
      digitalWrite(doBreakRelayOff, 1);
    }
  // Handle local control
  } else if (manualControlActive){
    digitalWrite(doCalibLampOn, 0);
    digitalWrite(doBreakRelayOff, 1);
    regulationValueIn = analogRead(aiLocalPot);
    regulationValueOut = map(regulationValueIn, 0, 1024, 0, fiveVoltValue);
  // Handle brake
  } else {
    digitalWrite(doCalibLampOn, 0);
    digitalWrite(doBreakRelayOff, 0);
    regulationValueIn = analogRead(aiLocalPot);
    //regulationValueOut = map(regulationValueIn, 0, 1024, 0, fiveVoltValue);
    regulationValueOut = 0; // Function should never output other than 0 if brake is active
  }
  analogWrite(aoMotorController, regulationValueOut);
}

void PrintSerial(){
  // print the results to the Serial Monitor:
  Serial.print("sensor = ");
  Serial.print(regulationValueIn);
  Serial.print("\t output = ");
  Serial.println(regulationValueOut);

  // wait 2 milliseconds before the next loop for the analog-to-digital
  // converter to settle after the last reading:
  delay(2);
}
