const int aiLocalPot = A2;  // Analog input pin that the potentiometer is attached to
const int aoMotorController = 9; // Analog output pin that the ESC is attached to

const int diRemoteOperationInput = 2;
const int diLocalOperationInput = 3;
const int diCalibButton = 4;
const int diRemoteInputThrottle = 7;  // Digital input pin that the receiver throttle is attached to

const int doBreakRelayOff = 5;  // Digital output pin that switches break relay. High turns break off
const int doCalibLampOn = 6;  // Digital output pin that indicates calibration

const int breakActivationValue = 3;
const int fiveVoltValue = 92;
const int calibCycles = 100;
int remoteLowValue = 1582;
int remoteHighValue = 2014;
long remoteCalibValue = 2014;

int regulationValueIn = 0;        // value read from the pot
int regulationValueOut = 0;        // value output to the PWM (analog out)
int remoteBreakSwitch = 0;        // value read from the pot

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
  CalibRemote;
  RunMotor;
  PrintSerial;
}

void CalibRemote() {
  while (digitalRead(diCalibButton) == LOW) {
    digitalWrite(doCalibLampOn,1);
    delay(1000);
    digitalWrite(doCalibLampOn,0);
    remoteCalibValue = 0;
    for (int i = 1; i <= calibCycles; i++) {
      regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
      remoteCalibValue = remoteCalibValue + regulationValueIn;
      delay(30);
    }
    remoteLowValue = remoteCalibValue / calibCycles;
    Serial.print("remote low = ");
    Serial.print(remoteLowValue);
    digitalWrite(doCalibLampOn,1);
    while (int(regulationValueIn) <= int(remoteLowValue)+200) {
      regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
      delay(10);
    }
    digitalWrite(doCalibLampOn,0);
    remoteCalibValue = 0;
    delay(1000);
    for (int i = 2; i <= calibCycles; i++) {
      regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
      remoteCalibValue = remoteCalibValue + regulationValueIn;
      delay(30);
    }
    remoteHighValue = remoteCalibValue / calibCycles;
    Serial.print("remote high = ");
    Serial.println(remoteHighValue);
    digitalWrite(doCalibLampOn,1);
    while (digitalRead(diCalibButton) == LOW) {
      delay(100);
    }
    digitalWrite(doCalibLampOn,0);
  }
}

void RunMotor() {
  if (digitalRead(diRemoteOperationInput) == LOW) {
    digitalWrite(doCalibLampOn,1);
    regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
    regulationValueOut = map(regulationValueIn, remoteLowValue, remoteHighValue, 0, fiveVoltValue);
    if (int (regulationValueOut ) <= breakActivationValue) {
      digitalWrite(doBreakRelayOff,0);
  } else {
      digitalWrite(doBreakRelayOff,1);
    }
} else if (digitalRead(diLocalOperationInput) == LOW){
    digitalWrite(doCalibLampOn,0);
    digitalWrite(doBreakRelayOff,1);
    regulationValueIn = analogRead(aiLocalPot);
    regulationValueOut = map(regulationValueIn, 0, 1024, 0, fiveVoltValue);
} else {
    digitalWrite(doCalibLampOn,0);
    digitalWrite(doBreakRelayOff,0);
    regulationValueIn = analogRead(aiLocalPot);
    regulationValueOut = map(regulationValueIn, 0, 1024, 0, fiveVoltValue);
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
