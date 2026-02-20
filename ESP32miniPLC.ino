#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Stepper.h>

// ================== DISPLAY (ST7789V2 240x280) ==================
#define TFT_CS   5
#define TFT_DC   21
#define TFT_RST  22
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ================== RELAY ==================
#define RELAY_OUT 25        // flip logic below if active-LOW relay
const bool RELAY_ACTIVE_LOW = false;

// ================== STEPPER (28BYJ-48 + ULN2003) ==================
#define STEPS_PER_REV 2048
#define M_IN1 26
#define M_IN2 27
#define M_IN3 14
#define M_IN4 12
Stepper motor(STEPS_PER_REV, M_IN1, M_IN3, M_IN2, M_IN4);

// ================== PLC STATE ==================
bool faultLatched = false;

bool manualRunLatch = false;   // start/stop latch (manual)
bool jogMode = false;          // jog on/off (manual)

bool autoMode = false;         // MANUAL/AUTO
bool autoRun = false;          // auto run state

int motorSpeed = 10;           // RPM 1..18
long stepsCounter = 0;         // counts motor.step(1) calls (+/-)

int lastScreenState = -999;

// AUTO pattern timings (ms)
unsigned long autoLastToggleMs = 0;
unsigned long autoRunMs = 4000;   // run duration
unsigned long autoStopMs = 2000;  // stop duration
bool autoPhaseRunning = false;    // internal phase

// ================== HMI ==================
void drawStaticUI() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 10);
  tft.println("ESP32 MINI PLC");

  tft.drawLine(0, 40, 240, 40, ST77XX_WHITE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 55);
  tft.println("STATE:");

  tft.setCursor(10, 150);
  tft.println("MODE:");

  tft.setCursor(10, 190);
  tft.println("SPD:");

  tft.setCursor(10, 230);
  tft.println("STEPS:");
}

void showBigState(const char* txt, uint16_t color) {
  tft.fillRect(10, 80, 220, 55, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(color);
  tft.setCursor(10, 90);
  tft.println(txt);
}

void showModeLine() {
  tft.fillRect(80, 150, 150, 25, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(autoMode ? ST77XX_YELLOW : ST77XX_WHITE);
  tft.setCursor(80, 150);
  tft.print(autoMode ? "AUTO" : "MANUAL");
}

void showSpeedLine() {
  tft.fillRect(70, 190, 160, 25, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(70, 190);
  tft.print(motorSpeed);
  tft.print(" RPM");
}

void showStepsLine() {
  tft.fillRect(90, 230, 140, 25, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(90, 230);
  tft.print(stepsCounter);
}

void updateHMI(bool runCmd, bool relayOn) {
  // state id encodes everything important to avoid flicker
  int st = 0;
  st |= (faultLatched ? 1 : 0) << 0;
  st |= (runCmd ? 1 : 0) << 1;
  st |= (relayOn ? 1 : 0) << 2;
  st |= (autoMode ? 1 : 0) << 3;
  st |= (autoRun ? 1 : 0) << 4;
  st |= (motorSpeed & 0x1F) << 5; // small

  if (st == lastScreenState) return;
  lastScreenState = st;

  if (faultLatched) {
    showBigState("FAULT", ST77XX_ORANGE);
  } else if (runCmd) {
    showBigState("RUN", ST77XX_GREEN);
  } else {
    showBigState("STOP", ST77XX_RED);
  }

  showModeLine();
  showSpeedLine();
  showStepsLine();
}

// ================== SERIAL ==================
String readLine() {
  if (!Serial.available()) return "";
  String s = Serial.readStringUntil('\n');
  s.trim();
  s.toLowerCase();
  return s;
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  start / stop              (manual latch)");
  Serial.println("  jog on / jog off           (manual momentary)");
  Serial.println("  fault / reset");
  Serial.println("  mode auto / mode manual");
  Serial.println("  auto start / auto stop     (enables/disables auto-run)");
  Serial.println("  speed N                    (1..18)");
  Serial.println("  autoparam runms X stopms Y (example: autoparam runms 4000 stopms 2000)");
  Serial.println("  steps reset");
  Serial.println("  status");
  Serial.println("  help");
}

void handleCmd(const String& cmd) {
  if (cmd.length() == 0) return;

  if (cmd == "help") { printHelp(); return; }

  if (cmd == "fault") {
    faultLatched = true;
    manualRunLatch = false;
    jogMode = false;
    autoRun = false;
    Serial.println("OK fault");
    lastScreenState = -999;
    return;
  }

  if (cmd == "reset") {
    faultLatched = false;
    manualRunLatch = false;
    jogMode = false;
    autoRun = false;
    Serial.println("OK reset");
    lastScreenState = -999;
    return;
  }

  if (cmd == "start") {
    if (!faultLatched) manualRunLatch = true;
    Serial.println("OK start");
    lastScreenState = -999;
    return;
  }

  if (cmd == "stop") {
    manualRunLatch = false;
    jogMode = false;
    Serial.println("OK stop");
    lastScreenState = -999;
    return;
  }

  if (cmd == "jog on") {
    if (!faultLatched) jogMode = true;
    Serial.println("OK jog on");
    lastScreenState = -999;
    return;
  }

  if (cmd == "jog off") {
    jogMode = false;
    Serial.println("OK jog off");
    lastScreenState = -999;
    return;
  }

  if (cmd == "mode auto") {
    autoMode = true;
    manualRunLatch = false;
    jogMode = false;
    Serial.println("OK mode auto");
    lastScreenState = -999;
    return;
  }

  if (cmd == "mode manual") {
    autoMode = false;
    autoRun = false;
    Serial.println("OK mode manual");
    lastScreenState = -999;
    return;
  }

  if (cmd == "auto start") {
    if (!faultLatched) {
      autoMode = true;
      autoRun = true;
      autoPhaseRunning = false;
      autoLastToggleMs = millis();
    }
    Serial.println("OK auto start");
    lastScreenState = -999;
    return;
  }

  if (cmd == "auto stop") {
    autoRun = false;
    Serial.println("OK auto stop");
    lastScreenState = -999;
    return;
  }

  if (cmd.startsWith("speed ")) {
    int v = cmd.substring(6).toInt();
    if (v < 1) v = 1;
    if (v > 18) v = 18;
    motorSpeed = v;
    motor.setSpeed(motorSpeed);
    Serial.print("OK speed ");
    Serial.println(motorSpeed);
    lastScreenState = -999;
    return;
  }

  if (cmd == "steps reset") {
    stepsCounter = 0;
    Serial.println("OK steps reset");
    lastScreenState = -999;
    return;
  }

  // Example: autoparam runms 4000 stopms 2000
  if (cmd.startsWith("autoparam")) {
    // very simple parse
    long r = autoRunMs;
    long s = autoStopMs;

    int iRun = cmd.indexOf("runms");
    int iStop = cmd.indexOf("stopms");
    if (iRun >= 0) {
      r = cmd.substring(iRun + 5).toInt();
    }
    if (iStop >= 0) {
      s = cmd.substring(iStop + 6).toInt();
    }
    if (r < 200) r = 200;
    if (s < 200) s = 200;
    autoRunMs = (unsigned long)r;
    autoStopMs = (unsigned long)s;
    Serial.print("OK autoparam runms ");
    Serial.print(autoRunMs);
    Serial.print(" stopms ");
    Serial.println(autoStopMs);
    return;
  }

  // ===== STATUS for GUI polling =====
  if (cmd == "status") {
    bool runCmd = false;
    bool relayOn = false;

    // compute runCmd same way as loop does
    if (faultLatched) runCmd = false;
    else if (autoMode && autoRun) runCmd = autoPhaseRunning;
    else runCmd = (manualRunLatch || jogMode);

    relayOn = runCmd && !faultLatched;

    Serial.print("STATUS ");
    Serial.print("MODE=");  Serial.print(autoMode ? "AUTO" : "MANUAL");
    Serial.print(" RUN=");   Serial.print(runCmd ? 1 : 0);
    Serial.print(" FAULT="); Serial.print(faultLatched ? 1 : 0);
    Serial.print(" RELAY="); Serial.print(relayOn ? 1 : 0);
    Serial.print(" SPEED="); Serial.print(motorSpeed);
    Serial.print(" STEPS="); Serial.print(stepsCounter);
    Serial.print(" ARUN=");  Serial.print(autoRun ? 1 : 0);
    Serial.print(" ARUNMS=");Serial.print(autoRunMs);
    Serial.print(" ASTOPMS=");Serial.println(autoStopMs);
    return;
  }

  Serial.println("ERR unknown (type help)");
}

// ================== OUTPUT HELPERS ==================
void setRelay(bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_OUT, on ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_OUT, on ? HIGH : LOW);
  }
}

// ================== AUTO LOGIC ==================
void autoUpdatePhases() {
  if (!autoMode || !autoRun || faultLatched) {
    autoPhaseRunning = false;
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - autoLastToggleMs;

  if (autoPhaseRunning) {
    if (elapsed >= autoRunMs) {
      autoPhaseRunning = false;
      autoLastToggleMs = now;
    }
  } else {
    if (elapsed >= autoStopMs) {
      autoPhaseRunning = true;
      autoLastToggleMs = now;
    }
  }
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(RELAY_OUT, OUTPUT);
  setRelay(false);

  motor.setSpeed(motorSpeed);

  tft.init(240, 280);
  tft.setRotation(0);
  drawStaticUI();

  Serial.println("ESP32 PLC ready. Type: help");
}

void loop() {
  // read command
  String cmd = readLine();
  if (cmd.length()) handleCmd(cmd);

  // update auto phase
  autoUpdatePhases();

  // compute runCmd
  bool runCmd = false;
  if (faultLatched) {
    runCmd = false;
  } else if (autoMode && autoRun) {
    runCmd = autoPhaseRunning;
  } else {
    runCmd = (manualRunLatch || jogMode);
  }

  bool relayOn = runCmd && !faultLatched;
  setRelay(relayOn);

  updateHMI(runCmd, relayOn);

  // run motor in tiny steps so STOP is responsive via serial
  if (runCmd) {
    motor.step(1);
    stepsCounter += 1;
  } else {
    delay(2);
  }
}