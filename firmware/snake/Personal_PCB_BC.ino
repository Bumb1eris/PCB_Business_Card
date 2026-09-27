#include <SoftwareWire.h>

// --- CONFIGURATION ---
const int SLEEP_TIMEOUT_MINUTES = 1;  // Minutes of inactivity before turning off LEDs

// --- Pin Definitions ---
const int rowPins[] = {0, 1, 2, 3, 4};
const int colPins[] = {16, 8, 9, 10, 11, 12, 13};

// --- I2C Configuration ---
const int I2C_SDA_PIN = 14; 
const int I2C_SCL_PIN = 15; 
const int MMA_ADDR = 0x1C;
SoftwareWire i2c(I2C_SDA_PIN, I2C_SCL_PIN);

// --- MMA8451 Registers ---
const int REG_CTRL_REG1 = 0x2A;
const int REG_OUT_X_MSB = 0x01;

// --- Display Buffer ---
bool display[5][7];
int brightness[5][7];

// --- Snake Game Variables ---
struct SnakeSegment {
  int x, y;
};

SnakeSegment snake[35];
int snakeLength = 1;
int headX = 3, headY = 2;

// Food position
int foodX = 5, foodY = 1;
unsigned long foodSpawnTime = 0;

// Game state
bool gameOver = false;
int score = 0;

// Movement
int dirX = 1, dirY = 0;
unsigned long lastMoveTime = 0;
const int MOVE_INTERVAL = 500;

// --- Sensor Data ---
float tiltX = 0;
float tiltY = 0;
float baseX = 0;
float baseY = 0;

// Smoothed sensor values
float smoothTiltX = 0;
float smoothTiltY = 0;
const float SENSOR_SMOOTHING = 0.3;

// Shake detection
float lastTiltX = 0;
float lastTiltY = 0;
int shakeCount = 0;
unsigned long lastShakeTime = 0;

// Sleep mode
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = SLEEP_TIMEOUT_MINUTES * 60000UL;
bool ledsOff = false;

// --- Game over animation ---
unsigned long gameOverStartTime = 0;
int scoreDisplayCount = 0;
unsigned long lastScoreLightTime = 0;
const int SCORE_LIGHT_DELAY = 400;

// --- Timing ---
unsigned long lastUpdate = 0;
const int UPDATE_INTERVAL = 30;

void setup() {
  // Initialize pins
  for (int i = 0; i < 5; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], LOW);
  }
  for (int i = 0; i < 7; i++) {
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], LOW);
  }
  pinMode(5, OUTPUT); digitalWrite(5, LOW);
  pinMode(6, OUTPUT); digitalWrite(6, LOW);
  pinMode(7, OUTPUT); digitalWrite(7, LOW);

  // Initialize I2C
  i2c.begin();
  delay(100);

  // Check accelerometer
  i2c.beginTransmission(MMA_ADDR);
  if (i2c.endTransmission() != 0) {
    while(1) {
      digitalWrite(rowPins[0], HIGH);
      digitalWrite(colPins[0], HIGH);
      delay(200);
      digitalWrite(colPins[0], LOW);
      digitalWrite(rowPins[0], LOW);
      delay(200);
    }
  }

  // Configure accelerometer
  i2c.beginTransmission(MMA_ADDR);
  i2c.write(REG_CTRL_REG1);
  i2c.write(0x00);
  i2c.endTransmission();
  delay(10);

  i2c.beginTransmission(MMA_ADDR);
  i2c.write(REG_CTRL_REG1);
  i2c.write(0x01);
  i2c.endTransmission();
  delay(10);

  // Calibrate
  float sumX = 0, sumY = 0;
  for (int i = 0; i < 20; i++) {
    float x, y;
    readAccel(x, y);
    sumX += x;
    sumY += y;
    delay(10);
  }
  baseX = sumX / 20.0;
  baseY = sumY / 20.0;

  // Initialize snake
  snake[0].x = 3;
  snake[0].y = 2;
  snakeLength = 1;

  // Place first food
  placeFood();
  
  lastActivityTime = millis();

  clearDisplay();
}

void loop() {
  unsigned long now = millis();
  
  // Check for timeout - turn off LEDs
  if (!ledsOff && (now - lastActivityTime > SLEEP_TIMEOUT)) {
    ledsOff = true;
    clearDisplay();
  }
  
  // If LEDs are off, check for wake (shake)
  if (ledsOff) {
    if (now - lastUpdate >= UPDATE_INTERVAL) {
      lastUpdate = now;
      
      float rawX, rawY;
      readAccel(rawX, rawY);
      tiltX = rawX - baseX;
      tiltY = rawY - baseY;
      
      // Use same shake detection as game reset
      float deltaX = abs(tiltX - lastTiltX);
      float deltaY = abs(tiltY - lastTiltY);
      
      if (deltaX > 3500 || deltaY > 3500) {
        shakeCount++;
        lastShakeTime = now;
      }
      
      if (now - lastShakeTime > 400) {
        shakeCount = 0;
      }
      
      // If shaken hard (5+ rapid movements), wake up and restart game
      if (shakeCount >= 5) {
        ledsOff = false;
        lastActivityTime = now;
        
        // Reset game
        snakeLength = 1;
        snake[0].x = 3;
        snake[0].y = 2;
        dirX = 1;
        dirY = 0;
        score = 0;
        gameOver = false;
        scoreDisplayCount = 0;
        shakeCount = 0;
        placeFood();
      }
      
      lastTiltX = tiltX;
      lastTiltY = tiltY;
    }
    return;
  }
  
  // Multiplex display
  for (int r = 0; r < 5; r++) {
    digitalWrite(rowPins[r], HIGH);
    for (int c = 0; c < 7; c++) {
      if (display[r][c]) {
        digitalWrite(colPins[c], HIGH);
        if (brightness[r][c] == 3) {
          delayMicroseconds(5);
        } else if (brightness[r][c] == 2) {
          delayMicroseconds(2);
        } else if (brightness[r][c] == 1) {
          delayMicroseconds(1);
        }
        digitalWrite(colPins[c], LOW);
      }
    }
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);
  }
  
  if (gameOver) {
    // Calculate target LED count based on score
    int targetLeds;
    if (score <= 23) {
      targetLeds = (score * 3 + 1) / 2;
    } else {
      targetLeds = score - 23;
    }
    
    // Animate score counting up
    if (now - lastScoreLightTime > SCORE_LIGHT_DELAY) {
      lastScoreLightTime = now;
      if (scoreDisplayCount < targetLeds) {
        scoreDisplayCount++;
        lastActivityTime = now;
      }
    }
    
    renderScrollingScore();
    
    // Check for shake to reset
    if (now - lastUpdate >= UPDATE_INTERVAL) {
      lastUpdate = now;
      float rawX, rawY;
      readAccel(rawX, rawY);
      tiltX = rawX - baseX;
      tiltY = rawY - baseY;
      
      float deltaX = abs(tiltX - lastTiltX);
      float deltaY = abs(tiltY - lastTiltY);
      
      if (deltaX > 3500 || deltaY > 3500) {
        shakeCount++;
        lastShakeTime = now;
        lastActivityTime = now;
      }
      
      if (now - lastShakeTime > 400) {
        shakeCount = 0;
      }
      
      if (shakeCount >= 5) {
        snakeLength = 1;
        snake[0].x = 3;
        snake[0].y = 2;
        dirX = 1;
        dirY = 0;
        score = 0;
        gameOver = false;
        shakeCount = 0;
        scoreDisplayCount = 0;
        lastActivityTime = now;
        placeFood();
      }
      
      lastTiltX = tiltX;
      lastTiltY = tiltY;
    }
    return;
  }

  // Update sensor reading
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;

    float rawX, rawY;
    readAccel(rawX, rawY);
    tiltX = rawX - baseX;
    tiltY = rawY - baseY;
    
    if (abs(tiltX) > 300 || abs(tiltY) > 300) {
      lastActivityTime = now;
    }
    
    smoothTiltX = smoothTiltX * (1.0 - SENSOR_SMOOTHING) + tiltX * SENSOR_SMOOTHING;
    smoothTiltY = smoothTiltY * (1.0 - SENSOR_SMOOTHING) + tiltY * SENSOR_SMOOTHING;

    updateDirection();
    
    lastTiltX = tiltX;
    lastTiltY = tiltY;
  }

  // Move snake
  if (now - lastMoveTime >= MOVE_INTERVAL) {
    lastMoveTime = now;
    lastActivityTime = now;
    moveSnake();
  }

  renderGame();
}

void readAccel(float &x, float &y) {
  i2c.beginTransmission(MMA_ADDR);
  i2c.write(REG_OUT_X_MSB);
  i2c.endTransmission(false);
  i2c.requestFrom(MMA_ADDR, 4);
  
  int16_t rawX = (i2c.read() << 8) | i2c.read();
  int16_t rawY = (i2c.read() << 8) | i2c.read();
  
  x = (float)(rawX >> 2);
  y = (float)(rawY >> 2);
}

void updateDirection() {
  float absX = abs(smoothTiltX);
  float absY = abs(smoothTiltY);
  
  const float THRESHOLD = 500.0;
  
  if (absX > THRESHOLD || absY > THRESHOLD) {
    int newDirX = 0;
    int newDirY = 0;
    
    if (absX > absY) {
      newDirX = (smoothTiltX > 0) ? 1 : -1;
      newDirY = 0;
    } else {
      newDirX = 0;
      newDirY = (smoothTiltY > 0) ? -1 : 1;
    }
    
    if (snakeLength >= 2) {
      int nextX = snake[0].x + newDirX;
      int nextY = snake[0].y + newDirY;
      
      if (nextX < 0) nextX = 6;
      if (nextX > 6) nextX = 0;
      if (nextY < 0) nextY = 4;
      if (nextY > 4) nextY = 0;
      
      if (nextX == snake[1].x && nextY == snake[1].y) {
        return;
      }
    }
    
    dirX = newDirX;
    dirY = newDirY;
  }
}

void moveSnake() {
  int newHeadX = snake[0].x + dirX;
  int newHeadY = snake[0].y + dirY;
  
  if (newHeadX < 0) newHeadX = 6;
  if (newHeadX > 6) newHeadX = 0;
  if (newHeadY < 0) newHeadY = 4;
  if (newHeadY > 4) newHeadY = 0;
  
  for (int i = 0; i < snakeLength; i++) {
    if (snake[i].x == newHeadX && snake[i].y == newHeadY) {
      gameOver = true;
      gameOverStartTime = millis();
      return;
    }
  }
  
  bool ateFood = false;
  if (newHeadX == foodX && newHeadY == foodY) {
    ateFood = true;
    score++;
    placeFood();
  }
  
  if (!ateFood) {
    for (int i = snakeLength - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
  } else {
    for (int i = snakeLength; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snakeLength++;
  }
  
  snake[0].x = newHeadX;
  snake[0].y = newHeadY;
}

void placeFood() {
  bool validPosition = false;
  
  while (!validPosition) {
    foodX = random(0, 7);
    foodY = random(0, 5);
    
    validPosition = true;
    for (int i = 0; i < snakeLength; i++) {
      if (snake[i].x == foodX && snake[i].y == foodY) {
        validPosition = false;
        break;
      }
    }
  }
  
  foodSpawnTime = millis();
}

void renderGame() {
  clearDisplay();
  
  for (int i = 0; i < snakeLength; i++) {
    display[snake[i].y][snake[i].x] = true;
    brightness[snake[i].y][snake[i].x] = 1;
  }
  
  unsigned long timeSinceSpawn = millis() - foodSpawnTime;
  bool showFood = true;
  
  if (timeSinceSpawn < 400) {
    int cycle = timeSinceSpawn % 100;
    showFood = (cycle >= 50);
  }
  
  if (showFood) {
    display[foodY][foodX] = true;
    brightness[foodY][foodX] = 3;
  }
}

void renderScrollingScore() {
  clearDisplay();
  
  int totalLeds;
  
  if (score <= 23) {
    totalLeds = (score * 3 + 1) / 2;
  } else {
    totalLeds = score - 23;
  }
  
  int ledsToShow = min(scoreDisplayCount, totalLeds);
  
  if (score <= 23) {
    for (int i = 0; i < ledsToShow && i < 35; i++) {
      int row = i / 7;
      int col = i % 7;
      
      display[row][col] = true;
      brightness[row][col] = 2;
    }
  } else {
    for (int i = 0; i < ledsToShow && i < 35; i++) {
      int col = i / 5;
      int row = 4 - (i % 5);
      
      display[row][col] = true;
      brightness[row][col] = 2;
    }
  }
}

void clearDisplay() {
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 7; c++) {
      display[r][c] = false;
      brightness[r][c] = 0;
    }
  }
}