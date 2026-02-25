#include <Adafruit_NeoPixel.h>

// ====================== CONFIG ======================
static const uint8_t LED_PIN = 6;
static const uint8_t BUZZER_PIN = 9;

static const uint8_t JOY_X_PIN = A0;
static const uint8_t JOY_Y_PIN = A1;
static const uint8_t JOY_BTN_PIN = 2;

static const int DEADZONE = 180;

static const uint8_t W = 8;
static const uint8_t H = 8;
static const uint16_t NUM_LEDS = W * H;

// ====================== TYPES ======================
struct Point {
  int8_t x;
  int8_t y;
};

enum Dir : uint8_t { UP, DOWN, LEFT, RIGHT };

// ====================== LED MATRIX ======================
class LedMatrix {
public:
  LedMatrix(uint16_t n, uint8_t pin)
  : strip(n, pin, NEO_GRB + NEO_KHZ800) {}

  void begin(uint8_t brightness) {
    strip.begin();
    strip.setBrightness(brightness);
    strip.show();
  }

  void clear() { strip.clear(); }

  void setXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= W || y >= H) return;
    strip.setPixelColor(xyToIndex(x, y), strip.Color(r, g, b));
  }

  void show() { strip.show(); }

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
  uint16_t xyToIndex(uint8_t x, uint8_t y) {
    return (uint16_t)y * W + x;
  }
};

// ====================== SOUND FX ======================
class SoundFX {
public:
  explicit SoundFX(uint8_t pin) : pin(pin) {}

  void begin() { pinMode(pin, OUTPUT); }

  void eatApple() {
    tone(pin, 880, 60);
    // petit “blip” double (optionnel)
    // delay(70); tone(pin, 1175, 50);
  }

  void gameOver() {
    tone(pin, 440, 140); delay(170);
    tone(pin, 330, 140); delay(170);
    tone(pin, 220, 220); delay(260);
    noTone(pin);
  }

private:
  uint8_t pin;
};

// ====================== INPUT JOYSTICK ======================
class InputJoystick {
public:
  void begin(uint8_t btnPin) {
    this->btnPin = btnPin;
    pinMode(btnPin, INPUT_PULLUP);
  }

  // Retourne true si clic (pour reset)
  bool read(Dir currentDir, Dir &pendingDir) {
    int x = analogRead(JOY_X_PIN) - 512;
    int y = analogRead(JOY_Y_PIN) - 512;

    // Axe dominant (évite diagonales)
    if (abs(x) > abs(y)) {
      if (x > DEADZONE) pendingDir = RIGHT;
      else if (x < -DEADZONE) pendingDir = LEFT;
    } else {
      // Si UP/DOWN inversé chez toi, inverse UP et DOWN ici
      if (y > DEADZONE) pendingDir = DOWN;
      else if (y < -DEADZONE) pendingDir = UP;
    }

    // Interdire demi-tour (on garde pending mais le jeu filtrera aussi)
    (void)currentDir;

    // Clic joystick
    if (digitalRead(btnPin) == LOW) {
      delay(90); // anti-rebond simple
      return true;
    }
    return false;
  }

private:
  uint8_t btnPin = 2;
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

  void triggerGameOver() {
    gameOverFlag = true;
    matrix.drawGameOverCross();
    sfx.gameOver();
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

  // collision en ignorant la queue (si on ne mange pas)
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

  void step() {
    if (!opposite(dir, pending)) dir = pending;

    Point head = snake[0];
    Point next = head;

    switch (dir) {
      case UP:    next.y--; break;
      case DOWN:  next.y++; break;
      case LEFT:  next.x--; break;
      case RIGHT: next.x++; break;
    }

    // murs
    if (next.x < 0 || next.x >= W || next.y < 0 || next.y >= H) {
      triggerGameOver();
      return;
    }

    bool ate = (next.x == apple.x && next.y == apple.y);

    // collision propre (queue ignorée si elle bouge)
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

    // Apple
    matrix.setXY(apple.x, apple.y, 0, 180, 0);

    // Snake: head white, body blue
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
InputJoystick input;
SnakeGame game(matrix, sfx);

void setup() {
  matrix.begin(25);       // UNO friendly
  sfx.begin();
  input.begin(JOY_BTN_PIN);
  game.begin();
}

void loop() {
  Dir pending = game.getDir();
  bool clicked = input.read(game.getDir(), pending);
  game.setPending(pending);

  if (clicked) {
    game.reset();
  }

  game.update();
}