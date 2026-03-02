#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "ICM_20948.h"
#include <math.h>
// ===================== CONFIG PINS =====================
static const uint8_t LED_PIN    = 6;   // WS2812 DIN
static const uint8_t BUZZER_PIN = 9;
static const uint8_t BTN_PIN    = 2;   // bouton restart -> GND (INPUT_PULLUP)
static const uint8_t LIFE1_PIN  = 3;   // LED vie 1
static const uint8_t LIFE2_PIN  = 4;   // LED vie 2
static const uint8_t LIFE3_PIN  = 5;   // LED vie 3

// ===================== MATRIX ==========================
static const uint8_t W = 8;
static const uint8_t H = 8;
static const uint16_t NUM_LEDS = W * H;


// ===================== TUNING IMU ======================
static const float LPF_WEIGHT      = 0.25f;   // plus réactif
static const float DEAD_DEG        = 8.0f;    // évite les faux positifs
static const float TRIG_DEG        = 18.0f;   // seuil déclenchement plus franc
static const float DOMINANCE       = 1.20f;   // moins strict
static const unsigned long DIR_COOLDOWN_MS = 80; // plus réactif

static const unsigned long IMU_READ_MS = 50;
static const uint8_t AD0_VAL = 1;
// ===================== GAME ============================
struct Point { int8_t x; int8_t y; };
enum Dir : uint8_t { UP, DOWN, LEFT, RIGHT };

static const unsigned long START_STEP_MS = 250;
static const unsigned long MIN_STEP_MS   = 90;
static const unsigned long SPEEDUP_MS    = 10;

// ===================== HARDWARE OBJECTS =================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
ICM_20948_I2C imu;

// ===================== UTIL LED MATRIX ==================
static inline uint16_t xyToIndex(uint8_t x, uint8_t y) {
  return (uint16_t)y * W + x; // mapping ligne par ligne (non serpentin)
}

static inline void setXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= W || y >= H) return;
  strip.setPixelColor(xyToIndex(x, y), strip.Color(r, g, b));
}

static void drawGameOverCross() {
  strip.clear();
  for (uint8_t i = 0; i < 8; i++) {
    setXY(i, i, 255, 0, 0);
    setXY(7 - i, i, 255, 0, 0);
  }
  strip.show();
}

// ===================== SOUND ============================
static inline void sfxEat()      { tone(BUZZER_PIN, 900, 80); }
static inline void sfxLoseLife() { tone(BUZZER_PIN, 300, 120); }
static inline void sfxGameOver() { tone(BUZZER_PIN, 200, 700); }

// ===================== INPUT (IMU) ======================
// Valeurs filtrées (LPF) et offsets (calibration repos)
static float fax = 0, fay = 0, faz = 0;
static float roll0 = 0, pitch0 = 0;

// Filtre passe-bas sur ax/ay/az, puis calc pitch/roll sur valeurs filtrées
static void updateFilteredAccelAndAngles(float &rollDeg, float &pitchDeg) {
  imu.getAGMT();

  // mg -> g (selon lib: accX() en mg)
  float ax = imu.accX() / 1000.0f;
  float ay = imu.accY() / 1000.0f;
  float az = imu.accZ() / 1000.0f;

  // LPF
  fax = (1.0f - LPF_WEIGHT) * fax + LPF_WEIGHT * ax;
  fay = (1.0f - LPF_WEIGHT) * fay + LPF_WEIGHT * ay;
  faz = (1.0f - LPF_WEIGHT) * faz + LPF_WEIGHT * az;

  // Angles (degrés) à partir des valeurs filtrées
  rollDeg  = atan2f(fay, faz) * 57.2958f;
  pitchDeg = atan2f(-fax, sqrtf(fay * fay + faz * faz)) * 57.2958f;
}

static void calibrateIMU() {
  const int N = 100;  // plus de samples
  
  // Chauffe le filtre d'abord (50 lectures ignorées)
  for (int i = 0; i < 50; i++) {
    float r, p;
    updateFilteredAccelAndAngles(r, p);
    delay(10);
  }
  
  float sumR = 0, sumP = 0;
  for (int i = 0; i < N; i++) {
    float r, p;
    updateFilteredAccelAndAngles(r, p);
    sumR += r;
    sumP += p;
    delay(10);
  }
  roll0  = sumR / N;
  pitch0 = sumP / N;
}

// Direction lue depuis roll/pitch (deltas par rapport au repos)
static Dir readIMUDirection(Dir current) {
  static unsigned long lastChange = 0;

  float rollDeg, pitchDeg;
  updateFilteredAccelAndAngles(rollDeg, pitchDeg);

  float dr = rollDeg  - roll0;
  float dp = pitchDeg - pitch0;

  // Normalise entre -180 et +180
  if (dr >  180.0f) dr -= 360.0f;
  if (dr < -180.0f) dr += 360.0f;

  // DEBUG
  Serial.print("dr="); Serial.print(dr);
  Serial.print("  dp="); Serial.println(dp);

  float adr = fabs(dr);
  float adp = fabs(dp);

  if (adr < DEAD_DEG && adp < DEAD_DEG) return current;

  unsigned long now = millis();
  if (now - lastChange < DIR_COOLDOWN_MS) return current;

  if (adr > adp * DOMINANCE) {
    if (dr >  TRIG_DEG) { lastChange = now; return LEFT;  }  // +67 = gauche
    if (dr < -TRIG_DEG) { lastChange = now; return RIGHT; }  // -57 = droite
  } else if (adp > adr * DOMINANCE) {
    if (dp >  TRIG_DEG) { lastChange = now; return DOWN;  }  // +59 = vers toi = bas
    if (dp < -TRIG_DEG) { lastChange = now; return UP;    }  // -41 = loin de toi = haut
  }

  return current;
}
// ===================== GAME STATE =======================
static Point snake[NUM_LEDS];
static uint8_t snakeLen = 3;
static Point apple;

static Dir dir = RIGHT;
static Dir pendingDir = RIGHT;

static unsigned long lastStep = 0;
static unsigned long stepDelay = START_STEP_MS;

static unsigned long lastIMURead = 0;

static int lives = 3;
static bool gameOver = false;

// ===================== LIVES LEDS =======================
static void updateLivesLED() {
  digitalWrite(LIFE1_PIN, lives >= 1 ? HIGH : LOW);
  digitalWrite(LIFE2_PIN, lives >= 2 ? HIGH : LOW);
  digitalWrite(LIFE3_PIN, lives >= 3 ? HIGH : LOW);
}

// ===================== GAME LOGIC =======================
static bool opposite(Dir a, Dir b) {
  return (a == UP && b == DOWN) || (a == DOWN && b == UP) ||
         (a == LEFT && b == RIGHT) || (a == RIGHT && b == LEFT);
}

static bool snakeHas(int x, int y) {
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

static void spawnApple() {
  while (true) {
    int x = random(0, W);
    int y = random(0, H);
    if (!snakeHas(x, y)) {
      apple = { (int8_t)x, (int8_t)y };
      return;
    }
  }
}

static void resetRound() {
  gameOver = false;
  dir = RIGHT;
  pendingDir = RIGHT;

  snakeLen = 3;
  snake[0] = {3, 4};
  snake[1] = {2, 4};
  snake[2] = {1, 4};

  stepDelay = START_STEP_MS;
  spawnApple();

  lastStep = millis();
}

static void fullRestart() {
  lives = 3;
  updateLivesLED();
  resetRound();
}

static void doGameOver() {
  gameOver = true;
  drawGameOverCross();
  sfxGameOver();
}

static void loseLife() {
  lives--;
  updateLivesLED();
  sfxLoseLife();

  if (lives <= 0) {
    doGameOver();
  } else {
    resetRound();
  }
}

static void drawGame() {
  strip.clear();

  // apple
  setXY(apple.x, apple.y, 0, 180, 0);

  // snake
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (i == 0) setXY(snake[i].x, snake[i].y, 255, 255, 255);
    else        setXY(snake[i].x, snake[i].y, 0, 0, 200);
  }

  strip.show();
}

static void stepGame() {
  // applique la direction pending si pas demi-tour
  if (!opposite(dir, pendingDir)) dir = pendingDir;

  Point next = snake[0];
  switch (dir) {
    case UP:    next.y--; break;
    case DOWN:  next.y++; break;
    case LEFT:  next.x--; break;
    case RIGHT: next.x++; break;
  }

  // mur
  if (next.x < 0 || next.x >= W || next.y < 0 || next.y >= H) {
    loseLife();
    return;
  }

  // collision avec soi
  if (snakeHas(next.x, next.y)) {
    loseLife();
    return;
  }

  bool ate = (next.x == apple.x && next.y == apple.y);

  if (ate && snakeLen < NUM_LEDS) {
    snakeLen++;
  }

  // décalage
  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0] = next;

  if (ate) {
    sfxEat();
    spawnApple();

    if (stepDelay > MIN_STEP_MS) {
      stepDelay = (stepDelay > SPEEDUP_MS) ? (stepDelay - SPEEDUP_MS) : stepDelay;
      if (stepDelay < MIN_STEP_MS) stepDelay = MIN_STEP_MS;
    }
  }
}

// ===================== BUTTON ===========================
static bool restartPressed() {
  static bool last = HIGH;
  bool now = digitalRead(BTN_PIN);
  bool pressed = (last == HIGH && now == LOW);
  last = now;
  return pressed;
}

// ===================== SETUP/LOOP =======================
void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);

  pinMode(LIFE1_PIN, OUTPUT);
  pinMode(LIFE2_PIN, OUTPUT);
  pinMode(LIFE3_PIN, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  strip.begin();
  strip.setBrightness(25);
  strip.show();
Serial.begin(9600);
  Wire.begin();
  Wire.setClock(400000);
  Wire.setWireTimeout(3000, true);

  randomSeed(analogRead(A3));

  imu.begin(Wire, AD0_VAL);

  delay(900);       // IMU immobile
  calibrateIMU();   // set roll0/pitch0

  fullRestart();
  drawGame();
}

void loop() {
  // restart toujours possible
  if (restartPressed()) {
    fullRestart();
    delay(80); // anti-rebond simple
  }

  if (gameOver) return;

  unsigned long now = millis();

  // lire IMU à intervalle fixe (stabilité)
  if (now - lastIMURead >= IMU_READ_MS) {
    lastIMURead = now;

    Dir d = readIMUDirection(dir);
    if (d != dir) pendingDir = d; // latch jusqu'au prochain step
  }

  // tick du jeu
  if (now - lastStep >= stepDelay) {
    lastStep = now;
    stepGame();

    if (gameOver) return;

    drawGame();
  }
}