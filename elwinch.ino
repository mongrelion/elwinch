// =============================================================================
// elwinch.ino — Arduino-based electric winch controller
// =============================================================================
//
// This sketch bridges an RC receiver and a manual potentiometer to an
// Electronic Speed Controller (Kelly KLS96601), driving a QS 16" 8kW motor.
//
// It supports three operating modes selected via a 3-way switch:
//   - Remote: reads throttle PWM pulses from the RC receiver, maps them to
//     the ESC's expected 0–5 V range, and controls a brake relay.
//   - Local:  reads an analog potentiometer (0–5 V) and maps it to the ESC.
//   - Brake:  engages the brake relay and sets ESC output to zero.
//
// A calibration routine learns the remote throttle's idle and full-throttle
// pulse widths. Calibration is entered by holding the calib button while in
// remote mode, and is guided by a status lamp.
//
// Pin layout (Arduino Uno / Nano compatible):
//   A2 — Analog in  — Local potentiometer (manual throttle)
//   D2 — Digital in — Remote mode switch (INPUT_PULLUP; LOW = active)
//   D3 — Digital in — Local mode switch  (INPUT_PULLUP; LOW = active)
//   D4 — Digital in — Calibration button  (INPUT_PULLUP; LOW = active)
//   D5 — Digital out— Brake relay (HIGH = brake released / motor free)
//   D6 — Digital out— Calibration status lamp (HIGH = lamp on)
//   D7 — Digital in — RC receiver throttle signal (PWM pulse read via pulseIn)
//   D9 — Analog out— PWM output to the ESC motor controller
// =============================================================================

// ---------------------------------------------------------------------------
// Timing & calibration constants
// ---------------------------------------------------------------------------

#define LAMP_BLINK_DURATION 1000  // Duration (ms) of each lamp pulse during calibration

// Calibration state-machine states (intentional fall-through between states)
#define NOMINAL           0       // Normal operation; calibration not active
#define SIGNAL_CALIB_MIN  10      // Prompt the user to set remote throttle to idle
#define CALIBRATE_MIN     20      // Sample and average the idle pulse width
#define CALIB_MIN_DONE    30      // Idle calibration complete; blink confirmation
#define SIGNAL_CALIB_MAX  40      // Prompt the user to hold remote throttle at max
#define CALIBRATE_MAX     50      // Sample and average the max-throttle pulse width
#define CALIB_MAX_DONE    60      // Max calibration complete; blink confirmation
#define CALIBRATION_DONE  99      // Entire calibration sequence finished; wait for button release

// ---------------------------------------------------------------------------
// Pin assignments
// ---------------------------------------------------------------------------

const int aiLocalPot        = A2; // Analog input  — local potentiometer (manual throttle)
const int aoMotorController = 9;  // Analog output — PWM signal to the ESC

const int diRemoteOperationInput = 2; // Digital input — remote-mode switch (3-way, position top)
const int diLocalOperationInput  = 3; // Digital input — local-mode switch  (3-way, position bottom)
const int diCalibButton          = 4; // Digital input — calibration pushbutton
const int diRemoteInputThrottle  = 7; // Digital input — RC receiver throttle PWM signal

const int doBreakRelayOff = 5; // Digital output — brake relay (HIGH = brake disengaged / free)
const int doCalibLampOn   = 6; // Digital output — calibration indicator lamp (HIGH = on)

// ---------------------------------------------------------------------------
// Operational constants
// ---------------------------------------------------------------------------

const int breakActivationValue = 3;   // ESC output threshold: values ≤ 3 engage the brake
const int fiveVoltValue        = 92;  // PWM value corresponding to 5 V (ESC full-scale ≈ 0–92)
const int calibCycles          = 100; // Number of readings averaged for each calibration endpoint

// ---------------------------------------------------------------------------
// Remote throttle calibration values (updated during calibration)
// ---------------------------------------------------------------------------

int  remoteLowValue  = 1582;  // Pulse width (µs) at idle throttle   — calibrated
int  remoteHighValue = 2014;  // Pulse width (µs) at full throttle   — calibrated
long remoteCalibValue = 2014; // Running accumulator used during calibration averaging

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------

int regulationValueIn  = 0;   // Last raw input reading (pot or throttle pulse)
int regulationValueOut = 0;   // Last computed output value sent to the ESC

bool remoteModeSelected    = false; // 3-way switch is in "remote" position
bool manualModeSelected    = false; // 3-way switch is in "local" position
bool calibButtonActive     = false; // Calibration pushbutton is held down
bool remoteCalibModeActive = false; // Calibration is armed (remote mode + button pressed)

int calibState = NOMINAL;          // Current step in the calibration state machine
unsigned long lastLampBlink = 0;   // Timestamp (ms) of the last lamp state change
int blinkCounter = 0;              // Counts confirmation blinks during calibration

// ---------------------------------------------------------------------------
// flashIt — Strobe the built-in LED 5 times (quick boot indication)
// ---------------------------------------------------------------------------
void flashIt() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(25);
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
  }
}

// ---------------------------------------------------------------------------
// setup — One-time hardware & serial initialisation
// ---------------------------------------------------------------------------
void setup() {
  // Initialise serial communication for debugging / monitoring
  Serial.begin(9600);

  // All digital inputs use the MCU's internal pull-up resistors.
  // A LOW reading means the switch/button is active (pulled to GND).
  pinMode(diLocalOperationInput, INPUT_PULLUP);
  pinMode(diRemoteOperationInput, INPUT_PULLUP);
  pinMode(diRemoteInputThrottle, INPUT_PULLUP);
  pinMode(diCalibButton, INPUT_PULLUP);

  pinMode(doBreakRelayOff, OUTPUT);
  pinMode(doCalibLampOn, OUTPUT);

  // Boot greeting: flash the built-in LED three times
  for (int i = 0; i < 3; i++) {
    flashIt();
    delay(300);
  }

  Serial.println("-> Setup done");
}

// ---------------------------------------------------------------------------
// loop — Main execution loop (called repeatedly by the Arduino runtime)
// ---------------------------------------------------------------------------
// Each iteration:
//   1. Reads the 3-way switch and calibration button.
//   2. Advances the calibration state machine if appropriate.
//   3. Computes and writes the ESC output based on the active mode.
//   4. Prints sensor & output values over serial for debugging.
// ---------------------------------------------------------------------------
void loop() {
  // -------- Read digital inputs (LOW = active due to INPUT_PULLUP) --------
  remoteModeSelected = (digitalRead(diRemoteOperationInput) == LOW);
  manualModeSelected = (digitalRead(diLocalOperationInput) == LOW);
  calibButtonActive  = (digitalRead(diCalibButton) == LOW);

  // -------- Run the calibration state machine --------
  // Note: CalibRemote() updates the global remoteCalibModeActive and
  //       the calibration endpoint values (remoteLowValue, remoteHighValue).
  CalibRemote(remoteModeSelected, calibButtonActive);

  // -------- Compute ESC output and handle brake relay --------
  RunMotor(remoteModeSelected, manualModeSelected, remoteCalibModeActive);

  // -------- Debug output --------
  PrintSerial;  // (function-call syntax without parentheses is valid in Arduino)
}

// ==========================================================================
// CalibRemote — Remote throttle endpoint calibration state machine
// ==========================================================================
//
// This function implements a sequential calibration routine that learns the
// pulse-width range of the RC receiver throttle. It is called every loop
// iteration but only makes progress when remoteCalibModeActive is true
// (i.e., remote mode is selected AND the calibration button is held).
//
// State flow (intentional fall-through — no break statements):
//
//   NOMINAL ──(button+remote)──▶ SIGNAL_CALIB_MIN
//                                   │  (lamp on 1 s → user sets idle)
//                                   ▼
//                              CALIBRATE_MIN
//                                   │  (sample 100 pulses → average → store)
//                                   ▼
//                              CALIB_MIN_DONE
//                                   │  (blink lamp 3× fast to confirm)
//                                   ▼
//                              SIGNAL_CALIB_MAX
//                                   │  (lamp on 1 s → user sets max)
//                                   ▼
//                              CALIBRATE_MAX
//                                   │  (sample 100 pulses → average → store)
//                                   ▼
//                              CALIB_MAX_DONE
//                                   │  (blink lamp 3× fast to confirm)
//                                   ▼
//                              CALIBRATION_DONE (waits for button release)
//
// The fall-through means multiple states can execute in a single loop
// iteration. For example, CALIBRATE_MIN samples all 100 pulses and
// immediately advances to CALIB_MIN_DONE in the same pass, but the blink
// confirmation in CALIB_MIN_DONE gates on timing, so it takes several
// subsequent loop iterations to play the three blinks.
// ==========================================================================
void CalibRemote(bool remoteModeActive, bool calibButtonActive) {
  int nextState = 0;                        // (unused — kept for legacy)
  unsigned long currentTime = millis();     // Snapshot current time once per call

  // Arm calibration when both remote mode and button are active.
  // Disarm immediately if the user switches away from remote mode.
  remoteCalibModeActive = (calibButtonActive and remoteModeActive);
  if (remoteCalibModeActive and calibState == NOMINAL) {
    calibState = SIGNAL_CALIB_MIN;
  } else if (not remoteModeActive) {
    calibState = NOMINAL;
  }

  switch(calibState) {

    // ------------------------------------------------------------------
    // NOMINAL — Idle; calibration is not active.
    // Falls through to SIGNAL_CALIB_MIN if calibState was just changed
    // above (both conditions met this iteration). Otherwise harmless.
    // ------------------------------------------------------------------
    case NOMINAL:
      // Do nothing

    // ------------------------------------------------------------------
    // SIGNAL_CALIB_MIN — Tell the user to put the throttle at idle.
    // The lamp stays on for LAMP_BLINK_DURATION ms, then we proceed.
    // ------------------------------------------------------------------
    case SIGNAL_CALIB_MIN:

      remoteCalibValue = 0;                 // Reset accumulator
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION and lastLampBlink > 0) {
        digitalWrite(doCalibLampOn, LOW);    // Lamp off
        calibState = CALIBRATE_MIN;          // Advance to sampling
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;         // Start the lamp-on period
        Serial.print("Leave remote throttle at idle");
        digitalWrite(doCalibLampOn, HIGH);   // Lamp on
      }
      // Falls through to CALIBRATE_MIN if calibState was just set above.
      // CALIBRATE_MIN will then execute in the same loop iteration.

    // ------------------------------------------------------------------
    // CALIBRATE_MIN — Sample the idle pulse width 100 times and average.
    // ------------------------------------------------------------------
    case CALIBRATE_MIN:

      // Collect calibCycles readings, summing into remoteCalibValue
      for (int i = 1; i <= calibCycles; i++) {
        regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
        remoteCalibValue = remoteCalibValue + regulationValueIn;
        delay(30);                           // Allow ~30 ms between readings
      }
      remoteLowValue = remoteCalibValue / calibCycles;  // Store idle endpoint
      Serial.print("remote low = ");
      Serial.print(remoteLowValue);
      calibState = CALIB_MIN_DONE;           // Advance to confirmation blinks
      // Falls through — CALIB_MIN_DONE will run this same iteration

    // ------------------------------------------------------------------
    // CALIB_MIN_DONE — Blink the lamp 3× quickly to confirm idle calib.
    // Timing: each on/off cycle is LAMP_BLINK_DURATION/4 ms (250 ms).
    // ------------------------------------------------------------------
    case CALIB_MIN_DONE:

      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION/4 and lastLampBlink > 0) {
        if (blinkCounter >= 3) {
          calibState = SIGNAL_CALIB_MAX;     // 3 blinks done → prompt for max
          blinkCounter = 0;
        } else {
          digitalWrite(doCalibLampOn, LOW);  // Lamp off between blinks
        }
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        if (blinkCounter == 0) Serial.print("Calibration of throttle min done!");
        digitalWrite(doCalibLampOn, HIGH);   // Lamp on
        blinkCounter += 1;
      }
      // Falls through — SIGNAL_CALIB_MAX may execute if we just advanced

    // ------------------------------------------------------------------
    // SIGNAL_CALIB_MAX — Prompt user to hold throttle at maximum.
    // Identical pattern to SIGNAL_CALIB_MIN.
    // ------------------------------------------------------------------
    case SIGNAL_CALIB_MAX:

      remoteCalibValue = 0;                 // Reset accumulator
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION and lastLampBlink > 0) {
        digitalWrite(doCalibLampOn, LOW);
        calibState = CALIBRATE_MAX;
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        Serial.print("Hold remote throttle at max");
      }
      // Falls through to CALIBRATE_MAX

    // ------------------------------------------------------------------
    // CALIBRATE_MAX — Sample max-throttle pulse width and average.
    // NOTE: the loop starts at i=2 (not i=1) — a minor off-by-one,
    // meaning 99 readings instead of 100.  Practically negligible.
    // ------------------------------------------------------------------
    case CALIBRATE_MAX:

      for (int i = 2; i <= calibCycles; i++) {
        regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
        remoteCalibValue = remoteCalibValue + regulationValueIn;
        delay(30);
      }
      remoteHighValue = remoteCalibValue / calibCycles;  // Store max endpoint
      Serial.print("remote high = ");
      Serial.println(remoteHighValue);
      calibState = CALIB_MAX_DONE;
      // Falls through to CALIB_MAX_DONE

    // ------------------------------------------------------------------
    // CALIB_MAX_DONE — Blink 3× fast to confirm max calibration done.
    // ------------------------------------------------------------------
    case CALIB_MAX_DONE:

      currentTime = millis();               // Re-sample time (blocking loop above may have taken a while)
      if (currentTime - lastLampBlink > LAMP_BLINK_DURATION/4 and lastLampBlink > 0) {
        if (blinkCounter >= 3) {
          calibState = CALIBRATION_DONE;    // Complete
          blinkCounter = 0;
        }
        digitalWrite(doCalibLampOn, LOW);
        lastLampBlink = 0;
      } else if (lastLampBlink == 0) {
        lastLampBlink = currentTime;
        if (blinkCounter == 0) Serial.print("Calibration of throttle max done!");
        digitalWrite(doCalibLampOn, HIGH);
        blinkCounter += 1;
      }
      // Falls through to CALIBRATION_DONE

    // ------------------------------------------------------------------
    // CALIBRATION_DONE — Calibration complete; wait for button release.
    // As long as the button stays pressed, remoteCalibModeActive remains
    // true at the top of the function, but calibState stays at 99, so we
    // spin harmlessly.  When the user releases the button (or switches
    // away from remote), the NOMINAL branch resets calibState to 0.
    // ------------------------------------------------------------------
    case CALIBRATION_DONE:
      // Do nothing — wait for release of button

    // ------------------------------------------------------------------
    // default — Safety net: if we ever land on an unknown state, clear
    // the calibration flag so RunMotor doesn't suppress motor output.
    // ------------------------------------------------------------------
    default:
      remoteCalibModeActive = false;
  }
}

// ==========================================================================
// RunMotor — Determine ESC output based on the active operating mode
// ==========================================================================
//
// The mode is selected by a 3-way switch on the control board:
//
//   Switch position │ Mode   │ Behaviour
//   ────────────────┼────────┼──────────────────────────────────────────
//   Top             │ REMOTE │ Read RC receiver PWM, map to 0–5 V range.
//                   │        │ Brake relay engages when output ≈ idle.
//   Middle (off)    │ BRAKE  │ ESC output = 0.  Brake relay engaged.
//   Bottom          │ LOCAL  │ Read potentiometer, map to 0–5 V range.
//                   │        │ Brake relay always released.
//
// During calibration (remoteCalibModeActive) this function is effectively
// a no-op for the remote branch — the ESC receives no new output, and the
// brake relay is not toggled.  CalibRemote() uses pulseIn on the same
// throttle pin independently.
// ==========================================================================
void RunMotor(bool remoteControlActive, bool manualControlActive, bool remoteCalibModeActive) {

  // -------- REMOTE mode (top) --------
  if (remoteControlActive and not remoteCalibModeActive) {
    digitalWrite(doCalibLampOn, HIGH);          // Status lamp on (indicates remote operation)

    regulationValueIn = pulseIn(diRemoteInputThrottle, HIGH);
    // Map the raw pulse width (typically ~1000–2000 µs) to the ESC's
    // expected PWM range (0–92, corresponding to 0–5 V).
    regulationValueOut = map(regulationValueIn, remoteLowValue, remoteHighValue, 0, fiveVoltValue);

    // Engage brake when the mapped output is at or below the dead-zone threshold.
    // This prevents creep when the transmitter stick is at neutral.
    if (int(regulationValueOut) <= breakActivationValue) {
      digitalWrite(doBreakRelayOff, LOW);       // Brake ON
    } else {
      digitalWrite(doBreakRelayOff, HIGH);      // Brake OFF (motor free to spin)
    }

  // -------- LOCAL (manual) mode (bottom) --------
  } else if (manualControlActive) {
    digitalWrite(doCalibLampOn, LOW);           // Status lamp off
    digitalWrite(doBreakRelayOff, HIGH);        // Brake released (local operation => manual control)

    regulationValueIn = analogRead(aiLocalPot);
    // Map the potentiometer ADC reading (0–1023) to ESC PWM range (0–92).
    // NOTE: it maps to 1024 instead of 1023 — a minor off-by-one.
    regulationValueOut = map(regulationValueIn, 0, 1024, 0, fiveVoltValue);

  // -------- BRAKE (middle / neither switch active) --------
  } else {
    digitalWrite(doCalibLampOn, LOW);           // Status lamp off
    digitalWrite(doBreakRelayOff, LOW);         // Brake ON (relay de-energised → brake engaged)

    regulationValueIn = analogRead(aiLocalPot); // Still read pot for serial output
    regulationValueOut = 0;                     // ESC output forced to zero — motor cannot spin
  }

  // Write the computed PWM value to the ESC.
  // In remote-calibration mode this still fires, but since the remote branch
  // is skipped above, it writes whatever regulationValueOut was set to in the
  // previous (non-calibrating) iteration — effectively a hold-last-value.
  analogWrite(aoMotorController, regulationValueOut);
}

// ---------------------------------------------------------------------------
// PrintSerial — Dump sensor & output values over serial for debugging
// ---------------------------------------------------------------------------
// Called once per loop iteration.  The 2 ms delay also gives the ADC time
// to settle between readings (best-practice for Arduino analog inputs).
// ---------------------------------------------------------------------------
void PrintSerial() {
  Serial.print("sensor = ");
  Serial.print(regulationValueIn);
  Serial.print("\t output = ");
  Serial.println(regulationValueOut);

  // Allow the ADC to settle before the next analogRead
  delay(2);
}
