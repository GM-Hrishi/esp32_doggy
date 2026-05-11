// ============================================================
//  TAMAGOTCHI DOG v9 — ESP32 + SSD1306 0.96" OLED
//  Author : Hrishi
//  Repo   : github.com/GM-Hrishi/tamagotchi-dog
//
//  HARDWARE
//  ─────────────────────────────────────────────────────────
//  OLED  : SSD1306 128x64, I2C — SDA=GPIO21, SCL=GPIO22
//  Sensor: IR/Light sensor module — DO=GPIO5
//          LOW  = bright room | HIGH = dark or finger
//
//  BEHAVIOURS  (SLEEP never random — dark sensor only)
//  ─────────────────────────────────────────────────────────
//  WALK  60%  Walks left/right, plays with ball, idle blink
//  EAT   20%  Walks to bowl, eats, bowl slides out
//  TALK  20%  Speech bubble 30% chance per blink cycle
//
//  NEW IN v9
//  ─────────────────────────────────────────────────────────
//  • Ball edge drift — drifts to center if stuck near edge
//  • Wake-up animation — stretch + shake on waking from sleep
//  • Idle blink — eyes close briefly every few seconds
//  • Yawn before sleep — mouth opens in sit phase before lying
//  • Dog walks toward ball on its own when ball is still
//  • Dust puff — pixel burst when ball stops rolling
//  • Drifting clouds — slow clouds during WALK and EAT
//  • Pet cap — max 1s continuous pet reaction
//  • Speech bubble hidden while petted, 30% show chance
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ╔══════════════════════════════════════════════════════════╗
// ║         EASY TWEAK — ALL CONFIGURABLE VALUES            ║
// ╠══════════════════════════════════════════════════════════╣

// ── frame rate ─────────────────────────────────────────────
#define FRAME_MS              33    // ms per frame (~30fps); lower = faster

// ── animation speeds ───────────────────────────────────────
#define ANIM_MS              160    // ms per walking leg-step tick
#define TAIL_MS              110    // ms per tail-wag tick
#define SUN_RAY_MS           400    // ms per sun ray twinkle
#define STAR_TWINKLE_MS      400    // ms per star twinkle

// ── behaviour timing ───────────────────────────────────────
#define BEHAVIOR_MS        10000    // ms before switching random behaviour
#define WALK_SPEED          1.2f    // pixels/frame dog walks

// ── behaviour weights (WALK + EAT must be < 100) ───────────
#define WALK_WEIGHT           60    // % chance of WALK
#define EAT_WEIGHT            20    // % chance of EAT (TALK gets remainder)

// ── eating ─────────────────────────────────────────────────
#define NOD_MS               300    // ms per head-nod tick while eating
#define EAT_DURATION        4000    // ms dog eats before bowl slides out
#define BOWL_SLIDE_SPEED    2.8f    // pixels/frame bowl exits screen

// ── sleeping ───────────────────────────────────────────────
#define ZZZ_SPAWN_MS        1400    // ms between ZZZ spawns
#define SLEEP_SIT_MS        2200    // ms dog sits (includes yawn) before lying
#define SLEEP_YAWN_START    800     // ms into sit phase when yawn begins
#define SLEEP_YAWN_HOLD     900     // ms yawn mouth stays open
#define SLEEP_REACT_MS      1000    // ms dog holds head up when petted asleep
#define SLEEP_BACK_MS        600    // ms dog settles back down after react

// ── wake-up animation ──────────────────────────────────────
#define WAKE_SHAKE_MS        600    // ms total body shake duration on wake
#define WAKE_SHAKE_SPEED      80    // ms per shake wobble tick
#define WAKE_STRETCH_MS      800    // ms stretch pose holds after shake

// ── idle blink ─────────────────────────────────────────────
#define BLINK_INTERVAL_MS   4000    // ms between idle blinks (approximate)
#define BLINK_HOLD_MS         80    // ms eyes stay closed during blink

// ── interaction / sensor ───────────────────────────────────
#define TAP_MAX_MS           600    // HIGH pulse shorter than this = tap
#define DOUBLE_TAP_MS        500    // max ms gap between taps for double-tap
#define PET_MAX_MS          1000    // max ms a single pet fires hearts/react
#define PET_REACT_MS        1200    // ms heart-eyes persist after finger lifted
#define PET_MOOD_MS        15000    // ms "recently petted" happy mood lasts

// ── jump ───────────────────────────────────────────────────
#define JUMP_HEIGHT           18    // peak pixel height of jump arc
#define JUMP_DURATION        500    // ms for one complete jump arc

// ── dark sleep ─────────────────────────────────────────────
#define DARK_SLEEP_TRIGGER_MS 7000  // ms sustained HIGH → dark sleep
#define DARK_CHECK_MS        30000  // ms between sensor checks while sleeping
#define DARK_WAKE_MS         20000  // ms awake after flashlight-wake

// ── talk ───────────────────────────────────────────────────
#define TALK_BLINK_MS         2200  // ms between speech bubble blink cycles
#define TALK_SHOW_CHANCE        30  // % chance bubble appears per blink (0-100)

// ── ball ───────────────────────────────────────────────────
#define BALL_RADIUS            4    // pixel radius of ball
#define BALL_ROLL_SPEED       2.5f  // pixels/frame ball rolls after nudge
#define BALL_BOUNCE_CHANCE     50   // % chance nudge = full bounce vs roll+stop
#define BALL_STOP_MIN         20    // min px ball rolls before stopping
#define BALL_STOP_MAX         50    // max px ball rolls before stopping
#define BALL_COLLIDE_DIST     10    // px distance dog-to-ball triggers nudge
#define BALL_EDGE_MARGIN      18    // px from screen edge = "stuck in corner"
#define BALL_DRIFT_SPEED     0.3f   // px/frame ball drifts back toward center
#define BALL_DRIFT_DELAY     3000   // ms ball sits at edge before drifting
#define BALL_SEEK_DELAY      5000   // ms ball is still before dog walks toward it
#define BALL_SPIN_MS           80   // ms per ball spin animation tick

// ── dust puff ──────────────────────────────────────────────
#define DUST_LIFE             18    // frames dust puff lives

// ── clouds ─────────────────────────────────────────────────
#define CLOUD_COUNT            3    // number of clouds on screen
#define CLOUD_SPEED_MIN      0.2f   // px/frame slowest cloud
#define CLOUD_SPEED_MAX      0.5f   // px/frame fastest cloud

// ╚══════════════════════════════════════════════════════════╝

// ── hardware ────────────────────────────────────────────────
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR  0x3C
#define SENSOR_PIN   5

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ────────────────────────────────────────────────────────────
//  ENUMS
// ────────────────────────────────────────────────────────────
enum Behavior  { WALK, EAT, TALK };
enum EatPhase  { EAT_WALK_TO, EAT_EATING, EAT_BOWL_OUT };
enum SleepPhase{ SLEEP_SIT, SLEEP_LIE, SLEEP_LIFTING, SLEEP_BACK };
enum WakePhase { WAKE_NONE, WAKE_SHAKING, WAKE_STRETCHING };
enum BallState { BALL_STILL, BALL_ROLLING_BOUNCE, BALL_ROLLING_STOP };

// ────────────────────────────────────────────────────────────
//  GLOBAL STATE
// ────────────────────────────────────────────────────────────
Behavior   behavior      = WALK;
EatPhase   eatPhase      = EAT_WALK_TO;
SleepPhase sleepPhase    = SLEEP_SIT;
WakePhase  wakePhase     = WAKE_NONE;
BallState  ballState     = BALL_STILL;
bool       isSleeping    = false;

// ── timers ──────────────────────────────────────────────────
unsigned long now             = 0;
unsigned long lastFrame       = 0;
unsigned long behaviorTimer   = 0;
unsigned long animTimer       = 0;
unsigned long tailTimer       = 0;
unsigned long sunTimer        = 0;
unsigned long starTimer       = 0;
unsigned long zzzTimer        = 0;
unsigned long eatNodTimer     = 0;
unsigned long eatDoneTimer    = 0;
unsigned long sleepPhaseTimer = 0;
unsigned long sleepReactTimer = 0;
unsigned long talkTimer       = 0;
unsigned long petStart        = 0;
unsigned long jumpTimer       = 0;
unsigned long blinkTimer      = 0;
unsigned long blinkStart      = 0;
unsigned long wakePhaseTimer  = 0;
unsigned long wakeShakeTimer  = 0;
unsigned long yawnTimer       = 0;
unsigned long ballStillTimer  = 0;
unsigned long ballSpinTimer   = 0;

// ── animation counters ──────────────────────────────────────
int animFrame   = 0;
int tailFrame   = 0;
int sunRayFrame = 0;
int starFrame   = 0;
int eatNodFrame = 0;
int woofIdx     = 0;
int jumpCount   = 0;
int ballSpin    = 0;
int wakeShakeOffset = 0;   // x wobble during shake (-1, 0, 1)
int wakeShakeTick   = 0;

// ── dog ─────────────────────────────────────────────────────
float dogX    = 30.0f;
int   dogY    = 48;
int   dogDir  = 1;
float bowlX   = 110.0f;
int   bowlSide= 1;

// ── ball ────────────────────────────────────────────────────
float ballX         = 100.0f;
float ballDir       = -1.0f;
float ballStopTarget= 0.0f;

// ── dust puff ───────────────────────────────────────────────
struct Dust { float x, y; int life; };
const int MAX_DUST = 6;
Dust dusts[MAX_DUST];

// ── clouds ──────────────────────────────────────────────────
struct Cloud {
  float x, y;
  float speed;
  int   w;     // width in pixels (height is fixed ~6px)
};
Cloud clouds[CLOUD_COUNT];

// ── interaction flags ───────────────────────────────────────
bool isPetted       = false;
bool petReacting    = false;
bool eatPaused      = false;
bool sleepReacting  = false;
bool talkVisible    = false;
bool isJumping      = false;
bool recentlyPetted = false;
bool isBlinking     = false;
bool isYawning      = false;
bool seekingBall    = false;   // dog walking toward ball on its own

unsigned long petMoodTimer = 0;

// ── dark sleep ──────────────────────────────────────────────
bool          darkSleeping   = false;
bool          darkWaking     = false;
unsigned long highSince      = 0;
unsigned long darkCheckTimer = 0;
unsigned long darkWakeTimer  = 0;

// ── tap / pet detection ─────────────────────────────────────
bool          lastPinState   = false;
unsigned long highStartTime  = 0;
unsigned long lastTapRelease = 0;
bool          doubleTapped   = false;

// ── floating hearts ─────────────────────────────────────────
struct Heart { float x, y; int life; };
const int MAX_HEARTS = 8;
Heart hearts[MAX_HEARTS];

// ── ZZZs ────────────────────────────────────────────────────
struct Zzz { float x, y; int life, idx; };
const int MAX_ZZZ = 4;
Zzz  zzzs[MAX_ZZZ];
int  zzzNext = 0;

// ── woof pool ───────────────────────────────────────────────
struct WoofEntry { const char* word; const char* emoji; };
WoofEntry woofPool[] = {
  {"WOOF!", ":)" },
  {"ARF!",  "<3" },
  {"WOOF?", ".." },
  {"ARF!",  ":D" },
  {"WOOF!", ":o" },
  {"ARF?",  ":/" },
};

// ────────────────────────────────────────────────────────────
//  CLOUD INIT
// ────────────────────────────────────────────────────────────
void initClouds() {
  for (int i = 0; i < CLOUD_COUNT; i++) {
    clouds[i].x     = random(0, SCREEN_W);
    clouds[i].y     = random(3, 18);
    clouds[i].speed = CLOUD_SPEED_MIN + (random(0,100) / 100.0f)
                      * (CLOUD_SPEED_MAX - CLOUD_SPEED_MIN);
    clouds[i].w     = random(18, 32);
  }
}

void updateClouds() {
  for (int i = 0; i < CLOUD_COUNT; i++) {
    clouds[i].x += clouds[i].speed;
    if (clouds[i].x > SCREEN_W + clouds[i].w) {
      // wrap to left, randomise y and speed
      clouds[i].x     = -(float)clouds[i].w;
      clouds[i].y     = random(3, 18);
      clouds[i].speed = CLOUD_SPEED_MIN + (random(0,100) / 100.0f)
                        * (CLOUD_SPEED_MAX - CLOUD_SPEED_MIN);
      clouds[i].w     = random(18, 32);
    }
  }
}

void drawClouds() {
  for (int i = 0; i < CLOUD_COUNT; i++) {
    int cx = (int)clouds[i].x;
    int cy = (int)clouds[i].y;
    int cw = clouds[i].w;
    // cloud = 3 overlapping ellipses
    display.fillRoundRect(cx,        cy+3, cw,    5, 2, WHITE);
    display.fillRoundRect(cx+2,      cy+1, cw-6,  5, 2, WHITE);
    display.fillRoundRect(cx+cw/2-4, cy,   cw/2,  5, 2, WHITE);
  }
}

// ────────────────────────────────────────────────────────────
//  WEIGHTED RANDOM BEHAVIOUR
// ────────────────────────────────────────────────────────────
Behavior weightedRandomBehavior(Behavior cur) {
  Behavior b;
  do {
    int r = random(0, 100);
    if      (r < WALK_WEIGHT)              b = WALK;
    else if (r < WALK_WEIGHT + EAT_WEIGHT) b = EAT;
    else                                   b = TALK;
  } while (b == cur);
  return b;
}

// ────────────────────────────────────────────────────────────
//  DUST PUFF
// ────────────────────────────────────────────────────────────
void spawnDust(float x, float y) {
  // spawn several tiny dust particles around ball stop point
  int offsets[][2] = {{-4,-1},{4,-1},{-3,-3},{3,-3},{0,-4},{-5,0},{5,0}};
  for (int i = 0; i < 6 && i < MAX_DUST; i++) {
    dusts[i].x    = x + offsets[i][0];
    dusts[i].y    = y + offsets[i][1];
    dusts[i].life = DUST_LIFE - i * 2;
  }
}

void updateDrawDust() {
  for (int i = 0; i < MAX_DUST; i++) {
    if (dusts[i].life > 0) {
      dusts[i].y -= 0.2f;
      dusts[i].life--;
      if (dusts[i].life > 4) {
        display.drawPixel((int)dusts[i].x, (int)dusts[i].y, WHITE);
        display.drawPixel((int)dusts[i].x+1, (int)dusts[i].y, WHITE);
      } else {
        display.drawPixel((int)dusts[i].x, (int)dusts[i].y, WHITE);
      }
    }
  }
}

// ────────────────────────────────────────────────────────────
//  BALL HELPERS
// ────────────────────────────────────────────────────────────
void resetBallPosition() {
  // place ball on opposite side of screen from dog
  if (dogX < 64) {
    ballX = random(85, 112);
  } else {
    ballX = random(15, 42);
  }
  ballState = BALL_STILL;
  ballStillTimer = now;
  seekingBall = false;
}

void nudgeBall() {
  ballDir       = (dogDir == 1) ? 1.0f : -1.0f;
  seekingBall   = false;
  ballStillTimer = now;
  if (random(0, 100) < BALL_BOUNCE_CHANCE) {
    ballState = BALL_ROLLING_BOUNCE;
  } else {
    ballState = BALL_ROLLING_STOP;
    float dist    = random(BALL_STOP_MIN, BALL_STOP_MAX);
    ballStopTarget = constrain(ballX + ballDir * dist,
                               BALL_RADIUS + 2.0f,
                               SCREEN_W - BALL_RADIUS - 2.0f);
  }
}

void updateBall() {
  // spin tick while rolling
  if (ballState != BALL_STILL && now - ballSpinTimer > BALL_SPIN_MS) {
    ballSpin = (ballSpin + 1) % 4;
    ballSpinTimer = now;
  }

  if (ballState == BALL_STILL) {
    // edge drift: if stuck near wall, drift back toward center
    bool nearEdge = (ballX < BALL_EDGE_MARGIN ||
                     ballX > SCREEN_W - BALL_EDGE_MARGIN);
    if (nearEdge && now - ballStillTimer > BALL_DRIFT_DELAY) {
      ballX += (ballX < 64) ? BALL_DRIFT_SPEED : -BALL_DRIFT_SPEED;
    }
    return;
  }

  ballX += ballDir * BALL_ROLL_SPEED;

  if (ballState == BALL_ROLLING_BOUNCE) {
    if (ballX >= SCREEN_W - BALL_RADIUS - 1) {
      ballX = SCREEN_W - BALL_RADIUS - 1; ballDir = -1.0f;
    }
    if (ballX <= BALL_RADIUS + 1) {
      ballX = BALL_RADIUS + 1; ballDir = 1.0f;
    }
    // settle when it returns near where it started (near dog side)
    if (abs(ballX - dogX) < 20) {
      ballState = BALL_STILL;
      ballStillTimer = now;
      spawnDust(ballX, dogY - BALL_RADIUS);
    }
  }

  if (ballState == BALL_ROLLING_STOP) {
    bool reached = (ballDir > 0) ? (ballX >= ballStopTarget)
                                 : (ballX <= ballStopTarget);
    if (reached) {
      ballX     = ballStopTarget;
      ballState = BALL_STILL;
      ballStillTimer = now;
      spawnDust(ballX, dogY - BALL_RADIUS);
    }
  }
}

// ────────────────────────────────────────────────────────────
//  SPAWN HELPERS
// ────────────────────────────────────────────────────────────
void spawnHeart(float x, float y) {
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].life <= 0) {
      hearts[i].x    = x + random(-8, 8);
      hearts[i].y    = y;
      hearts[i].life = 45;
      return;
    }
  }
}

void spawnZzz(float hx, float hy) {
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzs[i].life <= 0) {
      zzzs[i].x    = hx + 8;
      zzzs[i].y    = hy - 4;
      zzzs[i].life = 55;
      zzzs[i].idx  = zzzNext % 3;
      zzzNext++;
      return;
    }
  }
}

// ────────────────────────────────────────────────────────────
//  DRAW PRIMITIVES
// ────────────────────────────────────────────────────────────
void drawHeart(int x, int y, bool big = false) {
  if (!big) {
    display.drawPixel(x+1,y,   WHITE); display.drawPixel(x+3,y,   WHITE);
    display.drawPixel(x,  y+1, WHITE); display.drawPixel(x+1,y+1, WHITE);
    display.drawPixel(x+2,y+1, WHITE); display.drawPixel(x+3,y+1, WHITE);
    display.drawPixel(x+4,y+1, WHITE);
    display.drawPixel(x+1,y+2, WHITE); display.drawPixel(x+2,y+2, WHITE);
    display.drawPixel(x+3,y+2, WHITE);
    display.drawPixel(x+2,y+3, WHITE);
  } else {
    display.drawPixel(x+1,y,WHITE); display.drawPixel(x+2,y,WHITE);
    display.drawPixel(x+4,y,WHITE); display.drawPixel(x+5,y,WHITE);
    display.drawPixel(x,y+1,WHITE); display.drawPixel(x+6,y+1,WHITE);
    display.drawPixel(x,y+2,WHITE); display.drawPixel(x+6,y+2,WHITE);
    display.drawPixel(x+1,y+3,WHITE); display.drawPixel(x+5,y+3,WHITE);
    display.drawPixel(x+2,y+4,WHITE); display.drawPixel(x+4,y+4,WHITE);
    display.drawPixel(x+3,y+5,WHITE);
    for (int fy = 1; fy <= 4; fy++) {
      int lx = x+1, rx = x+5;
      if (fy == 3) { lx = x+2; rx = x+4; }
      if (fy == 4) { lx = x+3; rx = x+3; }
      for (int fx = lx; fx <= rx; fx++) display.drawPixel(fx, y+fy, WHITE);
    }
  }
}

void drawBall(int cx, int cy, int spin) {
  display.drawCircle(cx, cy, BALL_RADIUS, WHITE);
  switch (spin % 4) {
    case 0:
      display.drawLine(cx, cy-BALL_RADIUS+1, cx, cy+BALL_RADIUS-1, WHITE);
      display.drawLine(cx-BALL_RADIUS+1, cy, cx+BALL_RADIUS-1, cy, WHITE);
      break;
    case 1:
      display.drawLine(cx-2, cy+2, cx+2, cy-2, WHITE);
      display.drawLine(cx-1, cy-2, cx+2, cy+1, WHITE);
      break;
    case 2:
      display.drawLine(cx, cy-BALL_RADIUS+1, cx, cy+BALL_RADIUS-1, WHITE);
      display.drawLine(cx-BALL_RADIUS+2, cy, cx+BALL_RADIUS-2, cy, WHITE);
      break;
    case 3:
      display.drawLine(cx-2, cy-2, cx+2, cy+2, WHITE);
      display.drawLine(cx+1, cy-2, cx-2, cy+1, WHITE);
      break;
  }
}

void drawMoon(int cx, int cy) {
  display.fillCircle(cx, cy, 7, WHITE);
  display.fillCircle(cx+4, cy-2, 6, BLACK);
}

void drawStars(int sf) {
  int sx[] = {20, 45, 70, 95, 110, 32, 60, 85};
  int sy[] = { 5,  8,  4,  7,   5, 12,  9,  3};
  for (int i = 0; i < 8; i++) {
    if ((i + sf) % 2 == 0) {
      display.drawPixel(sx[i],   sy[i],   WHITE);
      display.drawPixel(sx[i]+1, sy[i],   WHITE);
      display.drawPixel(sx[i],   sy[i]+1, WHITE);
      display.drawPixel(sx[i]+1, sy[i]+1, WHITE);
    } else {
      display.drawPixel(sx[i], sy[i], WHITE);
    }
  }
}

void drawSun(int cx, int cy, int rf) {
  display.fillCircle(cx, cy, 4, WHITE);
  int rays[8][2] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
  for (int i = 0; i < 8; i++) {
    int len = ((i + rf) % 2 == 0) ? 7 : 6;
    display.drawLine(cx + rays[i][0]*5, cy + rays[i][1]*5,
                     cx + rays[i][0]*len, cy + rays[i][1]*len, WHITE);
  }
}

void drawBowl(int bx, int by) {
  display.fillRect(bx-7, by-5, 14, 5, WHITE);
  display.fillRect(bx-5, by-7, 10, 3, WHITE);
  display.drawPixel(bx-3, by-8, WHITE);
  display.drawPixel(bx,   by-8, WHITE);
  display.drawPixel(bx+3, by-8, WHITE);
}

void drawZzzs() {
  display.setTextColor(WHITE);
  display.setTextSize(1);
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzs[i].life > 0) {
      display.setCursor((int)zzzs[i].x, (int)zzzs[i].y);
      if      (zzzs[i].idx == 0) display.print("z");
      else if (zzzs[i].idx == 1) display.print("Z");
      else                       display.print("Zz");
    }
  }
}

void drawSpeechBubble(int dogCX, int dogTopY,
                      const char* word, const char* emoji) {
  bool flip = (dogCX > 64);
  int bx = flip ? dogCX - 50 : dogCX + 5;
  int by = dogTopY - 18;
  int bw = 42, bh = 20;
  if (bx < 1)      bx = 1;
  if (bx+bw > 127) bx = 127 - bw;
  if (by < 1)      by = 1;
  display.fillRoundRect(bx, by, bw, bh, 3, WHITE);
  display.drawRoundRect(bx, by, bw, bh, 3, BLACK);
  display.fillTriangle(bx+5, by+bh, bx+12, by+bh, dogCX, dogTopY, WHITE);
  display.drawLine(bx+5,  by+bh, dogCX, dogTopY, BLACK);
  display.drawLine(bx+12, by+bh, dogCX, dogTopY, BLACK);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(bx+3, by+3);  display.print(word);
  display.setCursor(bx+3, by+12); display.print(emoji);
}

// ────────────────────────────────────────────────────────────
//  DRAW DOG
//  shakeOff  = x offset for wake shake wobble
//  blinking  = eyes closed (idle blink)
//  yawning   = mouth wide open (pre-sleep yawn)
//  wakeStretch = front legs extended (wake stretch pose)
// ────────────────────────────────────────────────────────────
void drawDog(int cx, int by, int dir, int frame,
             bool sleeping, bool sitting, bool heartEyes,
             int tailWag, int nodOffset, int jumpOff,
             int shakeOff, bool blinking, bool yawning,
             bool wakeStretch) {

  int drawBy = by + jumpOff;
  int drawCX = cx + shakeOff;

  // tail
  int tbx = drawCX + dir * (-7);
  int tby = drawBy - 12;
  if (sleeping) {
    display.drawLine(drawCX+6, drawBy-2, drawCX+10, drawBy-3, WHITE);
    display.drawPixel(drawCX+11, drawBy-4, WHITE);
  } else if (sitting) {
    display.drawLine(tbx, tby, tbx - dir*3, tby+4, WHITE);
    display.drawPixel(tbx - dir*4, tby+5, WHITE);
  } else {
    if (tailWag == 0) {
      display.drawLine(tbx, tby, tbx - dir*2, tby-5, WHITE);
      display.drawPixel(tbx - dir*3, tby-6, WHITE);
    } else {
      display.drawLine(tbx, tby, tbx - dir*4, tby-3, WHITE);
      display.drawPixel(tbx - dir*5, tby-3, WHITE);
    }
  }

  // body
  if (sleeping) {
    display.fillRoundRect(drawCX-11, drawBy-8, 22, 8, 3, WHITE);
  } else if (sitting || wakeStretch) {
    display.fillRoundRect(drawCX-7, drawBy-14, 14, 14, 4, WHITE);
  } else {
    display.fillRoundRect(drawCX-8, drawBy-16, 16, 13, 3, WHITE);
  }

  // legs
  if (!sleeping) {
    if (wakeStretch) {
      // front legs extended forward
      display.fillRect(drawCX + dir*6,  drawBy-3, 3, 3, WHITE);
      display.fillRect(drawCX + dir*10, drawBy-2, 3, 2, WHITE);
      display.fillRect(drawCX - dir*4,  drawBy-5, 3, 5, WHITE);
    } else if (sitting) {
      display.fillRect(drawCX-4, drawBy-5, 3, 5, WHITE);
      display.fillRect(drawCX+1, drawBy-5, 3, 5, WHITE);
    } else {
      if (frame == 0) {
        display.fillRect(drawCX-7, drawBy-6, 3, 6, WHITE);
        display.fillRect(drawCX+4, drawBy-6, 3, 6, WHITE);
      } else {
        display.fillRect(drawCX-7, drawBy-4, 3, 4, WHITE);
        display.fillRect(drawCX+5, drawBy-8, 3, 8, WHITE);
        display.fillRect(drawCX-6, drawBy-8, 3, 8, WHITE);
        display.fillRect(drawCX+4, drawBy-4, 3, 4, WHITE);
      }
    }
  }

  // head
  int headX = drawCX + dir * 5;
  int headW = 13, headH = 11;
  int headY = sleeping ? (drawBy - 10 + nodOffset) : (drawBy - 22 + nodOffset);
  display.fillRoundRect(headX - headW/2, headY - headH/2, headW, headH, 3, WHITE);

  // snout
  int snoutX = headX + dir * 4;
  display.fillRoundRect(snoutX - 2, headY, 5, 4, 1, WHITE);

  if (yawning) {
    // wide open mouth — no nose fill, open rectangle
    display.fillRect(snoutX - 2, headY, 5, 5, WHITE);
    display.drawRect(snoutX - 1, headY + 1, 4, 4, BLACK);
    display.fillRect(snoutX - 1, headY, 3, 2, BLACK); // nose stays
  } else {
    display.fillRect(snoutX - 1, headY, 3, 2, BLACK); // normal nose
  }

  // eyes
  int eyeX = headX + dir * 1;
  int eyeY = headY - 1;
  if (heartEyes) {
    drawHeart(eyeX - 3, eyeY - 2);
    drawHeart(eyeX + 1, eyeY - 2);
  } else if (sleeping) {
    display.drawPixel(eyeX-1, eyeY+1, BLACK);
    display.drawPixel(eyeX,   eyeY,   BLACK);
    display.drawPixel(eyeX+1, eyeY,   BLACK);
    display.drawPixel(eyeX+3, eyeY+1, BLACK);
    display.drawPixel(eyeX+4, eyeY,   BLACK);
    display.drawPixel(eyeX+5, eyeY,   BLACK);
  } else if (blinking) {
    // closed blink — thin lines
    display.drawLine(eyeX-1, eyeY, eyeX+1, eyeY, BLACK);
    display.drawLine(eyeX+3, eyeY, eyeX+5, eyeY, BLACK);
  } else {
    display.fillRect(eyeX - 1, eyeY - 1, 2, 2, BLACK);
    display.fillRect(eyeX + 3, eyeY - 1, 2, 2, BLACK);
    display.drawPixel(eyeX,     eyeY - 1, WHITE);
    display.drawPixel(eyeX + 4, eyeY - 1, WHITE);
  }

  // beagle ears
  int earTopY = headY - headH/2;
  int earLX   = headX - dir * 4;
  int earRX   = headX + dir * 2;
  display.fillRoundRect(earLX - 5, earTopY - 1, 7, 11, 2, WHITE);
  display.fillRoundRect(earRX - 1, earTopY - 1, 5,  8, 2, WHITE);
  display.drawRoundRect(earLX - 5, earTopY - 1, 7, 11, 2, BLACK);
}

// ────────────────────────────────────────────────────────────
//  BEHAVIOUR MANAGEMENT
// ────────────────────────────────────────────────────────────
void startBehavior(Behavior b) {
  behavior      = b;
  behaviorTimer = now;
  animFrame     = 0;
  animTimer     = now;
  petReacting   = false;
  eatPaused     = false;
  isJumping     = false;
  isSleeping    = false;
  seekingBall   = false;
  wakePhase     = WAKE_NONE;

  if (b == EAT) {
    eatPhase = EAT_WALK_TO;
    bowlSide = (random(0,2) == 0) ? 1 : -1;
    bowlX    = (bowlSide == 1) ? 110.0f : 18.0f;
    dogDir   = bowlSide;
  }
  if (b == TALK) {
    talkTimer   = now;
    talkVisible = false;
    woofIdx     = recentlyPetted ? random(0,2) : random(2,6);
  }
}

void enterDarkSleep() {
  darkSleeping   = true;
  darkWaking     = false;
  darkCheckTimer = now;
  isSleeping     = true;
  sleepPhase     = SLEEP_LIE;
  sleepReacting  = false;
  isJumping      = false;
  petReacting    = false;
  eatPaused      = false;
  isPetted       = false;
  seekingBall    = false;
  wakePhase      = WAKE_NONE;
  isYawning      = false;
  zzzTimer       = now - ZZZ_SPAWN_MS;
  for (int i = 0; i < MAX_HEARTS; i++) hearts[i].life = 0;
}

// Start wake-up animation — shake then stretch then walk
void beginWakeAnimation() {
  wakePhase      = WAKE_SHAKING;
  wakePhaseTimer = now;
  wakeShakeTimer = now;
  wakeShakeTick  = 0;
  isSleeping     = false;
  for (int i = 0; i < MAX_ZZZ; i++) zzzs[i].life = 0;
}

void exitDarkSleep() {
  darkSleeping = false;
  darkWaking   = true;
  darkWakeTimer= now;
  beginWakeAnimation();
  resetBallPosition();
}

// ────────────────────────────────────────────────────────────
//  SETUP
// ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(800000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed");
    while (1);
  }
  display.clearDisplay();
  display.display();
  display.setTextWrap(false);

  pinMode(SENSOR_PIN, INPUT);
  randomSeed(analogRead(0));

  for (int i = 0; i < MAX_HEARTS; i++) hearts[i].life = 0;
  for (int i = 0; i < MAX_ZZZ;    i++) zzzs[i].life   = 0;
  for (int i = 0; i < MAX_DUST;   i++) dusts[i].life   = 0;

  lastPinState = (digitalRead(SENSOR_PIN) == HIGH);
  highSince    = millis();
  blinkTimer   = millis();

  initClouds();
  ballX = 100.0f;
  ballState = BALL_STILL;
  ballStillTimer = millis();

  startBehavior(WALK);
}

// ────────────────────────────────────────────────────────────
//  LOOP
// ────────────────────────────────────────────────────────────
void loop() {
  now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  // ════════════════════════════════════════════════════════
  //  STEP 1 — SENSOR EDGES
  // ════════════════════════════════════════════════════════
  bool pinHigh     = (digitalRead(SENSOR_PIN) == HIGH);
  bool risingEdge  = pinHigh  && !lastPinState;
  bool fallingEdge = !pinHigh && lastPinState;

  if (risingEdge)  highSince = now;
  if (fallingEdge) highSince = now;

  // ════════════════════════════════════════════════════════
  //  STEP 2 — DARK SLEEP STATE MACHINE
  // ════════════════════════════════════════════════════════
  if (!darkSleeping && !darkWaking) {
    if (pinHigh && (now - highSince >= DARK_SLEEP_TRIGGER_MS)) {
      enterDarkSleep();
    }
  }

  if (darkSleeping) {
    if (now - darkCheckTimer >= DARK_CHECK_MS) {
      darkCheckTimer = now;
      if (!pinHigh) exitDarkSleep();
    }
    goto ANIM_AND_DRAW;
  }

  if (darkWaking) {
    if (now - darkWakeTimer >= DARK_WAKE_MS) {
      if (pinHigh) { enterDarkSleep(); goto ANIM_AND_DRAW; }
      else          darkWaking = false;
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 3 — TAP / PET LOGIC
  // ════════════════════════════════════════════════════════
  {
    isPetted     = pinHigh;
    doubleTapped = false;

    if (risingEdge) {
      highStartTime  = now;
      recentlyPetted = true;
      petMoodTimer   = now;
      for (int i = 0; i < 3; i++) spawnHeart(dogX + dogDir*5, dogY - 28);
      petStart    = now;
      petReacting = true;

      if (isSleeping) {
        sleepReacting   = true;
        sleepReactTimer = now;
        sleepPhase      = SLEEP_LIFTING;
      }
      if (!isSleeping && behavior == EAT && eatPhase == EAT_EATING) {
        eatPaused = true;
      }
    }

    if (fallingEdge) {
      unsigned long pulseDuration = now - highStartTime;
      if (pulseDuration < TAP_MAX_MS) {
        unsigned long gap = now - lastTapRelease;
        if (gap < DOUBLE_TAP_MS && gap > 30) doubleTapped = true;
        lastTapRelease = now;
      }
      petReacting = false;
      if (!isSleeping && behavior == EAT) eatPaused = false;
    }

    if (doubleTapped && !isJumping && !isSleeping && wakePhase == WAKE_NONE) {
      petReacting   = false;
      isJumping     = true;
      jumpCount     = 0;
      jumpTimer     = now;
      behavior      = WALK;
      behaviorTimer = now;
      seekingBall   = false;
      for (int i = 0; i < 5; i++) spawnHeart(dogX, dogY - 30);
    }

    if (recentlyPetted && now - petMoodTimer > PET_MOOD_MS) recentlyPetted = false;

    bool petExpired = isPetted && (now - highStartTime > PET_MAX_MS);
    if (petReacting && (!isPetted || petExpired) && now - petStart > PET_REACT_MS) {
      petReacting = false;
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 4 — BEHAVIOUR TIMER
  // ════════════════════════════════════════════════════════
  if (!isJumping && !isSleeping && wakePhase == WAKE_NONE
      && now - behaviorTimer > BEHAVIOR_MS) {
    startBehavior(weightedRandomBehavior(behavior));
  }

  // ════════════════════════════════════════════════════════
  //  ANIM_AND_DRAW
  // ════════════════════════════════════════════════════════
  ANIM_AND_DRAW:

  // anim ticks
  if (now - animTimer > ANIM_MS)         { animFrame   = (animFrame+1)%2;   animTimer  = now; }
  if (now - tailTimer > TAIL_MS)         { tailFrame   = (tailFrame+1)%2;   tailTimer  = now; }
  if (now - sunTimer  > SUN_RAY_MS)      { sunRayFrame = (sunRayFrame+1)%2; sunTimer   = now; }
  if (now - starTimer > STAR_TWINKLE_MS) { starFrame   = (starFrame+1)%2;   starTimer  = now; }

  // idle blink (only when not doing anything special)
  if (!isBlinking && !isSleeping && !darkSleeping && wakePhase == WAKE_NONE) {
    if (now - blinkTimer > BLINK_INTERVAL_MS + random(0, 2000)) {
      isBlinking = true;
      blinkStart = now;
      blinkTimer = now;
    }
  }
  if (isBlinking && now - blinkStart > BLINK_HOLD_MS) {
    isBlinking = false;
  }

  // hearts
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].life > 0) { hearts[i].y -= 0.55f; hearts[i].life--; }
  }
  bool petActive = isPetted && !darkSleeping && (now - highStartTime < PET_MAX_MS);
  if (petActive && random(0,6) == 0) spawnHeart(dogX + dogDir*5, dogY - 28);

  // ZZZs
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzs[i].life > 0) {
      zzzs[i].y -= 0.3f; zzzs[i].x += 0.2f; zzzs[i].life--;
    }
  }

  // clouds (only during non-sleep)
  if (!isSleeping && !darkSleeping) updateClouds();

  // ball
  if (!isSleeping && !darkSleeping && behavior == WALK && wakePhase == WAKE_NONE) {
    if (ballState == BALL_STILL) {
      if (abs(dogX - ballX) < BALL_COLLIDE_DIST) {
        nudgeBall();
      } else if (!isPetted && !isJumping
                 && now - ballStillTimer > BALL_SEEK_DELAY) {
        // dog walks toward ball on its own
        seekingBall = true;
      }
    } else {
      seekingBall = false;
    }
  }
  if (!darkSleeping) updateBall();

  // jump arc
  int jumpOffset = 0;
  if (isJumping) {
    unsigned long elapsed = now - jumpTimer;
    unsigned long phase   = elapsed % JUMP_DURATION;
    float t   = (float)phase / JUMP_DURATION;
    float arc = 4.0f * t * (1.0f - t);
    jumpOffset = -(int)(arc * JUMP_HEIGHT);
    if (phase < 50 && jumpCount < 2) {
      spawnHeart(dogX, dogY + jumpOffset - 5);
      spawnHeart(dogX+5, dogY + jumpOffset - 8);
    }
    if (elapsed >= JUMP_DURATION) {
      jumpTimer = now; jumpCount++;
      if (jumpCount >= 2) { isJumping = false; jumpOffset = 0; }
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 5 — BEHAVIOUR UPDATE
  // ════════════════════════════════════════════════════════
  bool heartEyes  = false;
  bool sleeping   = false;
  bool sitting    = false;
  bool wakeStr    = false;
  bool blinkDraw  = isBlinking;
  bool yawnDraw   = false;
  int  nodOffset  = 0;
  int  shakeOff   = 0;

  // ── wake animation ──────────────────────────────────────
  if (wakePhase == WAKE_SHAKING) {
    sitting = true;
    // wobble left-right
    if (now - wakeShakeTimer > WAKE_SHAKE_SPEED) {
      wakeShakeTick++;
      wakeShakeOffset = (wakeShakeTick % 2 == 0) ? 2 : -2;
      wakeShakeTimer  = now;
    }
    shakeOff = wakeShakeOffset;
    if (now - wakePhaseTimer > WAKE_SHAKE_MS) {
      wakePhase      = WAKE_STRETCHING;
      wakePhaseTimer = now;
      wakeShakeOffset = 0;
    }
  } else if (wakePhase == WAKE_STRETCHING) {
    wakeStr = true;
    if (now - wakePhaseTimer > WAKE_STRETCH_MS) {
      wakePhase = WAKE_NONE;
      startBehavior(WALK);
    }
  } else if (isSleeping || darkSleeping) {

    sleeping = true;
    isYawning = false;

    if (sleepPhase == SLEEP_SIT) {
      sitting  = true;
      sleeping = false;

      // yawn during sit phase
      unsigned long sitElapsed = now - sleepPhaseTimer;
      if (sitElapsed > SLEEP_YAWN_START &&
          sitElapsed < SLEEP_YAWN_START + SLEEP_YAWN_HOLD) {
        isYawning = true;
      }
      yawnDraw = isYawning;

      if (sitElapsed > SLEEP_SIT_MS) {
        sleepPhase      = SLEEP_LIE;
        sleepPhaseTimer = now;
        isYawning       = false;
      }
    }
    if (sleepPhase == SLEEP_LIE) {
      sleeping = true;
      if (now - zzzTimer > ZZZ_SPAWN_MS) {
        spawnZzz(dogX + dogDir*5, dogY - 22);
        zzzTimer = now;
      }
    }
    if (sleepPhase == SLEEP_LIFTING) {
      sitting = true; sleeping = false; heartEyes = true;
      if (now - sleepReactTimer > SLEEP_REACT_MS) {
        sleepPhase = SLEEP_BACK; sleepPhaseTimer = now;
      }
    }
    if (sleepPhase == SLEEP_BACK) {
      sitting = true; sleeping = false;
      if (now - sleepPhaseTimer > SLEEP_BACK_MS) {
        sleepPhase = SLEEP_LIE; sleepReacting = false;
      }
    }

  } else {

    switch (behavior) {

      case WALK: {
        if (!isPetted && !isJumping) {
          if (seekingBall) {
            // walk toward ball
            dogDir = (ballX > dogX) ? 1 : -1;
            dogX  += dogDir * WALK_SPEED;
          } else {
            dogX += dogDir * WALK_SPEED;
            if (dogX > 110) dogDir = -1;
            if (dogX <  18) dogDir =  1;
          }
        }
        heartEyes = isPetted && petReacting;
        break;
      }

      case EAT: {
        if (eatPhase == EAT_WALK_TO) {
          float target = bowlX + (-bowlSide * 14.0f);
          if (abs(dogX - target) > 2.0f) {
            dogDir = (target > dogX) ? 1 : -1;
            dogX  += dogDir * WALK_SPEED;
          } else {
            eatPhase = EAT_EATING; eatNodTimer = now; eatDoneTimer = now;
            dogDir = bowlSide;
          }
        }
        if (eatPhase == EAT_EATING) {
          if (!eatPaused) {
            if (now - eatNodTimer > NOD_MS) {
              eatNodFrame = (eatNodFrame+1) % 2; eatNodTimer = now;
            }
            nodOffset = (eatNodFrame == 1) ? 3 : 0;
            if (now - eatDoneTimer > EAT_DURATION) eatPhase = EAT_BOWL_OUT;
          } else {
            heartEyes = true; nodOffset = 0;
          }
        }
        if (eatPhase == EAT_BOWL_OUT) {
          bowlX += bowlSide * BOWL_SLIDE_SPEED;
          if (bowlX < -20 || bowlX > 148)
            startBehavior(weightedRandomBehavior(EAT));
        }
        break;
      }

      case TALK: {
        heartEyes = isPetted && petReacting;
        if (now - talkTimer > TALK_BLINK_MS) {
          talkTimer   = now;
          talkVisible = (random(0, 100) < TALK_SHOW_CHANCE);
          if (talkVisible)
            woofIdx = recentlyPetted ? random(0,2) : random(2,6);
        }
        break;
      }
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 6 — DRAW
  // ════════════════════════════════════════════════════════
  display.clearDisplay();

  // background
  bool nightScene = (isSleeping || darkSleeping || behavior == TALK)
                    && wakePhase == WAKE_NONE;
  if (nightScene) {
    drawMoon(110, 10);
    drawStars(starFrame);
  } else {
    drawSun(12, 10, sunRayFrame);
    drawClouds();
  }

  // ground
  display.drawLine(0, dogY+1, 127, dogY+1, WHITE);

  // ball (always visible)
  drawBall((int)ballX, dogY - BALL_RADIUS, ballSpin);

  // dust puff
  updateDrawDust();

  // bowl
  if (!isSleeping && !darkSleeping && behavior == EAT)
    drawBowl((int)bowlX, dogY);

  // ZZZs
  if (isSleeping || darkSleeping) drawZzzs();

  // speech bubble
  if (!isSleeping && !darkSleeping && behavior == TALK
      && talkVisible && !isPetted)
    drawSpeechBubble((int)dogX, dogY - 26,
                     woofPool[woofIdx].word,
                     woofPool[woofIdx].emoji);

  // dog
  drawDog((int)dogX, dogY, dogDir, animFrame,
          sleeping, sitting, heartEyes,
          tailFrame, nodOffset, jumpOffset,
          shakeOff, blinkDraw, yawnDraw, wakeStr);

  // hearts
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].life > 0)
      drawHeart((int)hearts[i].x, (int)hearts[i].y, hearts[i].life > 28);
  }

  display.display();

  // MUST be last — edge detection for next frame
  lastPinState = pinHigh;
}
