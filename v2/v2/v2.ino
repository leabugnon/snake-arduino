#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "ICM_20948.h"
#include <math.h>

// ====================== CONFIG ======================
static const uint8_t LED_PIN = 6;
static const uint8_t BUZZER_PIN = 9;
static const uint8_t RESET_BTN_PIN = 2;  // bouton vers GND (INPUT_PULLUP)

static const uint8_t W = 8;
static const uint8_t H = 8;
static const uint16_t NUM_LEDS = W * H;

// Ton scanner a trouvé 0x69 -> AD0_VAL = 1
static const uint8_t AD0_VAL = 1;

// Sensibilité tilt (en g) après calibration (delta). Ajuste si besoin.
static const float DEAD_G = 0.08f;  // zone morte
static const float TRIG_G = 0.18f;  // seuil direction

// ====================== TYPES ======================
struct Point { int8_t x; int8_t y; };
enum Dir : uint8_t { UP, DOWN, LEFT, RIGHT };

// ====================== LED MATRIX ======================
class LedMatrix {
public:
  LedMatrix(uint16_t n, uint8_t pin) : strip(n, pin, NEO_GRB + NEO_KHZ800) {}

  void begin(uint8_t brightness) {
    strip.begin();
    strip.setBrightness(brightness);
    strip.show();
  }

  void clear() { strip.clear(); }
  void show() { strip.show(); }

  void setXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= W || y >= H) return;
    strip.setPixelColor(xyToIndex(x, y), strip.Color(r, g, b));
  }

  void drawGameOverCross() {
    clear();
    for (uint8_t i = 0; i < 8; i++) {
      setXY(i, i, 255, 0, 0);
      setXY(7 - i, i, 255, 0, 0);
    }
    show();
  }

private:
  Adafruit_NeoPixel strip;

  // Mapping NON serpentin (ligne par ligne même sens)
  uint16_t xyToIndex(uint8_t x, uint8_t y) { return (uint16_t)y * W + x; }
};

// ====================== SOUND FX ======================
class SoundFX {
public:
  explicit SoundFX(uint8_t pin) : pin(pin) {}
  void begin() { pinMode(pin, OUTPUT); }

  void eatApple() { tone(pin, 880, 60); }

  void gameOver() {
    tone(pin, 440, 140); delay(170);
    tone(pin, 330, 140); delay(170);
    tone(pin, 220, 220); delay(260);
    noTone(pin);
  }

private:
  uint8_t pin;
};

// ====================== INPUT ICM-20948 (SparkFun) ======================
class InputICM20948 {
public:
  bool begin() {
    Wire.begin();
    Wire.setClock(400000);

    imu.begin(Wire, AD0_VAL);
    if (imu.status != ICM_20948_Stat_Ok) return false;

    calibrateZero(); // IMPORTANT: capteur immobile au démarrage
    return true;
  }

  Dir readDir(Dir current) {
    // Lecture capteur
    imu.getAGMT();

    // accX/accY souvent en mg -> on convertit en g
    float ax = imu.accX() / 1000.0f;
    float ay = imu.accY() / 1000.0f;

    // delta par rapport au repos
    float dx = ax - ax0;
    float dy = ay - ay0;

    // lissage léger
    fx = fx * 0.80f + dx * 0.20f;
    fy = fy * 0.80f + dy * 0.20f;

    // zone morte
    if (fabs(fx) < DEAD_G && fabs(fy) < DEAD_G) return current;

    // axe dominant
    if (fabs(fx) > fabs(fy)) {
      if (fx > TRIG_G && current != LEFT)  return RIGHT;
      if (fx < -TRIG_G && current != RIGHT) return LEFT;
    } else {
      // Si UP/DOWN est inversé, inverse ces deux lignes :
      if (fy > TRIG_G && current != UP)    return DOWN;
      if (fy < -TRIG_G && current != DOWN) return UP;
    }

    return current;
  }

private:
  ICM_20948_I2C imu;

  float ax0 = 0.0f, ay0 = 0.0f; // offset repos
  float fx = 0.0f, fy = 0.0f;   // filtres

  void calibrateZero() {
    const int N = 50;
    float sx = 0.0f, sy = 0.0f;

    for (int i = 0; i < N; i++) {
      imu.getAGMT();
      sx += imu.accX() / 1000.0f;
      sy += imu.accY() / 1000.0f;
      delay(10);
    }
    ax0 = sx / N;
    ay0 = sy / N;
  }
};

// ====================== SNAKE GAME ======================
class SnakeGame {
public:
  SnakeGame(LedMatrix &m, SoundFX &s) : matrix(m), sfx(s) {}

  void begin() {
    randomSeed(analogRead(A3));
    reset();
  }

  void reset() {
    gameOverFlag = false;
    dir = RIGHT;
    pending = RIGHT;
    stepMs = 300;
    lastStep = millis();

    len = 3;
    snake[0] = {3, 4};
    snake[1] = {2, 4};
    snake[2] = {1, 4};

    spawnApple();
    draw();
  }

  bool isGameOver() const { return gameOverFlag; }
  Dir getDir() const { return dir; }
  void setPending(Dir d) { pending = d; }

  void update() {
    if (gameOverFlag) {
      matrix.drawGameOverCross();
      return;
    }

    unsigned long now = millis();
    if (now - lastStep >= stepMs) {
      lastStep = now;
      step();
    }
    draw();
  }

private:
  LedMatrix &matrix;
  SoundFX &sfx;

  Point snake[NUM_LEDS];
  uint8_t len = 3;
  Point apple{0, 0};

  Dir dir = RIGHT;
  Dir pending = RIGHT;

  bool gameOverFlag = false;
  unsigned long lastStep = 0;
  unsigned long stepMs = 300;

  bool opposite(Dir a, Dir b) {
    return (a == UP && b == DOWN) || (a == DOWN && b == UP) ||
           (a == LEFT && b == RIGHT) || (a == RIGHT && b == LEFT);
  }

  bool snakeHas(int x, int y) {
    for (uint8_t i = 0; i < len; i++) {
      if (snake[i].x == x && snake[i].y == y) return true;
    }
    return false;
  }

  // collision propre: ignore la queue si elle va bouger (si on ne mange pas)
  bool snakeHasExceptTail(int x, int y) {
    if (len == 0) return false;
    for (uint8_t i = 0; i < len - 1; i++) {
      if (snake[i].x == x && snake[i].y == y) return true;
    }
    return false;
  }

  void spawnApple() {
    while (true) {
      int ax = random(0, W);
      int ay = random(0, H);
      if (!snakeHas(ax, ay)) {
        apple = {(int8_t)ax, (int8_t)ay};
        return;
      }
    }
  }

  void triggerGameOver() {
    gameOverFlag = true;
    matrix.drawGameOverCross();
    sfx.gameOver();
  }

  void step() {
    if (!opposite(dir, pending)) dir = pending;

    Point next = snake[0];
    switch (dir) {
      case UP:    next.y--; break;
      case DOWN:  next.y++; break;
      case LEFT:  next.x--; break;
      case RIGHT: next.x++; break;
    }

    if (next.x < 0 || next.x >= W || next.y < 0 || next.y >= H) {
      triggerGameOver();
      return;
    }

    bool ate = (next.x == apple.x && next.y == apple.y);

    if (ate) {
      if (snakeHas(next.x, next.y)) { triggerGameOver(); return; }
    } else {
      if (snakeHasExceptTail(next.x, next.y)) { triggerGameOver(); return; }
    }

    if (ate && len < NUM_LEDS) len++;

    for (int i = len - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snake[0] = next;

    if (ate) {
      sfx.eatApple();
      spawnApple();
      if (stepMs > 90) stepMs -= 12;
    }
  }

  void draw() {
    matrix.clear();
    matrix.setXY(apple.x, apple.y, 0, 180, 0);

    for (uint8_t i = 0; i < len; i++) {
      if (i == 0) matrix.setXY(snake[i].x, snake[i].y, 220, 220, 220);
      else        matrix.setXY(snake[i].x, snake[i].y, 0, 0, 180);
    }
    matrix.show();
  }
};

// ====================== GLOBALS ======================
LedMatrix matrix(NUM_LEDS, LED_PIN);
SoundFX sfx(BUZZER_PIN);
InputICM20948 inputIMU;
SnakeGame game(matrix, sfx);

void setup() {
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  matrix.begin(25);
  sfx.begin();

  // IMPORTANT: laisse l'IMU immobile 1 seconde après l'allumage
  delay(800);

  if (!inputIMU.begin()) {
    // Si erreur IMU: affiche croix mais laisse le jeu tourner
    matrix.drawGameOverCross();
    delay(1500);
  }

  game.begin();
}

void loop() {
  // reset manuel
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    game.reset();
    delay(200);
  }

  // lecture IMU -> direction
  Dir pending = inputIMU.readDir(game.getDir());
  game.setPending(pending);

  game.update();
}