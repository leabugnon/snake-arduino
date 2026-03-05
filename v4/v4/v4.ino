#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "ICM_20948.h"
#include <math.h>

// ===================== CONFIG PINS =====================
static const uint8_t LED_PIN    = 6;
static const uint8_t BUZZER_PIN = 9;
static const uint8_t BTN_PIN    = 2;
static const uint8_t LIFE1_PIN  = 3;
static const uint8_t LIFE2_PIN  = 4;
static const uint8_t LIFE3_PIN  = 5;

// ===================== MATRIX ==========================
static const uint8_t W = 16;
static const uint8_t H = 16;
static const uint16_t NUM_LEDS = W * H; // 256

// ===================== TUNING IMU ======================
static const float LPF_WEIGHT          = 0.25f;
static const float DEAD_DEG            = 8.0f;
static const float TRIG_DEG            = 18.0f;
static const float DOMINANCE           = 1.20f;
static const unsigned long DIR_COOLDOWN_MS = 80;
static const unsigned long IMU_READ_MS     = 50;
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

// ===================== MAPPING MATRICE 2x2 =============
//
//  Disposition physique (vue de face) :
//  [ Mat 1 (0) ] --> [ Mat 2 (1) ]
//                         |
//  [ Mat 4 (3) ] <-- [ Mat 3 (2) ]
//
//  Chaque matrice est 8x8, LEDs internes linéaires (même sens).
//  Le chaînage global est serpentin : mat0 → mat1 → mat2 → mat3
//
//  Coordonnées globales : x in [0..15], y in [0..15]
//    x=0..7  → colonne gauche  (mat 0 ou 3)
//    x=8..15 → colonne droite  (mat 1 ou 2)
//    y=0..7  → rangée haute    (mat 0 ou 1)
//    y=8..15 → rangée basse    (mat 2 ou 3)

static inline uint16_t xyToIndex(uint8_t x, uint8_t y) {
  if (x >= W || y >= H) return 0;

  uint8_t mx = x / 8;   // 0=gauche, 1=droite
  uint8_t my = y / 8;   // 0=haut,   1=bas
  uint8_t lx = x % 8;   // position locale X dans la matrice
  uint8_t ly = y % 8;   // position locale Y dans la matrice

  // Numéro de matrice (0..3) selon le serpentin
  // Rangée haute : gauche(0)=mat0,  droite(1)=mat1
  // Rangée basse : droite(1)=mat2,  gauche(0)=mat3  (sens inversé)
  uint8_t matIndex;
  if (my == 0) {
    matIndex = mx;        // mat0 ou mat1
  } else {
    matIndex = 3 - mx;    // mat2 (droite) ou mat3 (gauche)
  }

  uint16_t localIndex = (uint16_t)ly * 8 + lx;
  return (uint16_t)matIndex * 64 + localIndex;
}

static inline void setXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= W || y >= H) return;
  strip.setPixelColor(xyToIndex(x, y), strip.Color(r, g, b));
}

// ===================== GAME OVER : croix X ============
static void drawGameOverCross() {
  strip.clear();
  // Diagonale haut-gauche → bas-droite
  for (uint8_t i = 0; i < 16; i++) {
    setXY(i, i, 255, 0, 0);
  }
  // Diagonale haut-droite → bas-gauche
  for (uint8_t i = 0; i < 16; i++) {
    setXY(15 - i, i, 255, 0, 0);
  }
  strip.show();
}

// ===================== SOUND ============================
static inline void sfxEat()      { tone(BUZZER_PIN, 900, 80);  }
static inline void sfxLoseLife() { tone(BUZZER_PIN, 300, 120); }
static inline void sfxGameOver() { tone(BUZZER_PIN, 200, 700); }

// ===================== IMU / FILTRE =====================
static float fax = 0, fay = 0, faz = 0;
static float roll0 = 0, pitch0 = 0;

static void updateFilteredAccelAndAngles(float &rollDeg, float &pitchDeg) {
  imu.getAGMT();
  float ax = imu.accX() / 1000.0f;
  float ay = imu.accY() / 1000.0f;
  float az = imu.accZ() / 1000.0f;

  fax = (1.0f - LPF_WEIGHT) * fax + LPF_WEIGHT * ax;
  fay = (1.0f - LPF_WEIGHT) * fay + LPF_WEIGHT * ay;
  faz = (1.0f - LPF_WEIGHT) * faz + LPF_WEIGHT * az;

  rollDeg  = atan2f(fay, faz) * 57.2958f;
  pitchDeg = atan2f(-fax, sqrtf(fay * fay + faz * faz)) * 57.2958f;
}

static void calibrateIMU() {
  // Chauffe le filtre (50 lectures ignorées)
  for (int i = 0; i < 50; i++) {
    float r, p;
    updateFilteredAccelAndAngles(r, p);
    delay(10);
  }
  // Moyenne sur 100 lectures pour l'offset de repos
  float sumR = 0, sumP = 0;
  const int N = 100;
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

static Dir readIMUDirection(Dir current) {
  static unsigned long lastChange = 0;

  float rollDeg, pitchDeg;
  updateFilteredAccelAndAngles(rollDeg, pitchDeg);

  float dr = rollDeg  - roll0;
  float dp = pitchDeg - pitch0;

  // Normalise entre -180 et +180
  if (dr >  180.0f) dr -= 360.0f;
  if (dr < -180.0f) dr += 360.0f;

  float adr = fabs(dr);
  float adp = fabs(dp);

  // Zone neutre
  if (adr < DEAD_DEG && adp < DEAD_DEG) return current;

  // Cooldown anti-rebond
  unsigned long now = millis();
  if (now - lastChange < DIR_COOLDOWN_MS) return current;

  // Axe dominant → direction
  if (adr > adp * DOMINANCE) {
    if (dr >  TRIG_DEG) { lastChange = now; return LEFT;  }
    if (dr < -TRIG_DEG) { lastChange = now; return RIGHT; }
  } else if (adp > adr * DOMINANCE) {
    if (dp >  TRIG_DEG) { lastChange = now; return DOWN;  }
    if (dp < -TRIG_DEG) { lastChange = now; return UP;    }
  }

  return current;
}

// ===================== GAME STATE =======================
static Point snake[NUM_LEDS];
static uint8_t snakeLen = 3;
static Point apple;
static Dir dir        = RIGHT;
static Dir pendingDir = RIGHT;
static unsigned long lastStep    = 0;
static unsigned long stepDelay   = START_STEP_MS;
static unsigned long lastIMURead = 0;
static int  lives    = 3;
static bool gameOver = false;

// ===================== LIVES LEDS =======================
static void updateLivesLED() {
  digitalWrite(LIFE1_PIN, lives >= 1 ? HIGH : LOW);
  digitalWrite(LIFE2_PIN, lives >= 2 ? HIGH : LOW);
  digitalWrite(LIFE3_PIN, lives >= 3 ? HIGH : LOW);
}

// ===================== GAME LOGIC =======================
static bool opposite(Dir a, Dir b) {
  return (a == UP    && b == DOWN)  || (a == DOWN  && b == UP) ||
         (a == LEFT  && b == RIGHT) || (a == RIGHT && b == LEFT);
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
  gameOver   = false;
  dir        = RIGHT;
  pendingDir = RIGHT;
  // snakeLen gardé intentionnellement
  // on remet juste le serpent au centre avec sa taille actuelle
  for (uint8_t i = 0; i < snakeLen; i++) {
    snake[i] = { (int8_t)(5 - i), 8 };
  }
  stepDelay  = START_STEP_MS;
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
    snakeLen = 3; // reset taille seulement au vrai game over
    doGameOver();
  } else {
    resetRound(); // garde snakeLen
  }
}

static void drawGame() {
  strip.clear();
  // Pomme en vert
  setXY(apple.x, apple.y, 0, 180, 0);
  // Serpent : tête blanche, corps bleu
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (i == 0) setXY(snake[i].x, snake[i].y, 255, 255, 255);
    else        setXY(snake[i].x, snake[i].y, 0,   0,   200);
  }
  strip.show();
}

static void stepGame() {
  if (!opposite(dir, pendingDir)) dir = pendingDir;

  Point next = snake[0];
  switch (dir) {
    case UP:    next.y--; break;
    case DOWN:  next.y++; break;
    case LEFT:  next.x--; break;
    case RIGHT: next.x++; break;
  }

  // Collision mur
  if (next.x < 0 || next.x >= W || next.y < 0 || next.y >= H) {
    loseLife();
    return;
  }

  // Collision avec soi-même
  if (snakeHas(next.x, next.y)) {
    loseLife();
    return;
  }

  bool ate = (next.x == apple.x && next.y == apple.y);
  if (ate && snakeLen < NUM_LEDS) snakeLen++;

  // Décalage du corps
  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0] = next;

  if (ate) {
    sfxEat();
    spawnApple();
    stepDelay = max((long)stepDelay - (long)SPEEDUP_MS, (long)MIN_STEP_MS);
  }
}

// ===================== BUTTON ===========================
static bool restartPressed() {
  static bool last = HIGH;
  bool now     = digitalRead(BTN_PIN);
  bool pressed = (last == HIGH && now == LOW);
  last = now;
  return pressed;
}

// ===================== SETUP / LOOP =====================
void setup() {
  pinMode(BTN_PIN,   INPUT_PULLUP);
  pinMode(LIFE1_PIN, OUTPUT);
  pinMode(LIFE2_PIN, OUTPUT);
  pinMode(LIFE3_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  strip.begin();
  strip.setBrightness(25);
  strip.show();

  Wire.begin();
  Wire.setClock(400000);
  Wire.setWireTimeout(3000, true);

  randomSeed(analogRead(A3));
  imu.begin(Wire, AD0_VAL);

  delay(900);       // IMU immobile
  calibrateIMU();   // établit roll0 / pitch0

  fullRestart();
  drawGame();
}

void loop() {
  // Restart toujours possible
  if (restartPressed()) {
    fullRestart();
    delay(80);
  }

  if (gameOver) return;

  unsigned long now = millis();

  // Lecture IMU à intervalle fixe
  if (now - lastIMURead >= IMU_READ_MS) {
    lastIMURead = now;
    Dir d = readIMUDirection(dir);
    if (d != dir) pendingDir = d;
  }

  // Tick du jeu
  if (now - lastStep >= stepDelay) {
    lastStep = now;
    stepGame();
    if (!gameOver) drawGame();
  }
}
