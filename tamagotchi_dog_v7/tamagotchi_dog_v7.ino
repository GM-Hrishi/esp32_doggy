// ============================================================
//  TAMAGOTCHI DOG v7 — ESP32 + SSD1306 0.96" OLED
//  Author : Hrishi
//  Repo   : github.com/GM-Hrishi/tamagotchi-dog
//
//  HARDWARE
//  ─────────────────────────────────────────────────────────
//  OLED  : SSD1306 128x64, I2C — SDA=GPIO21, SCL=GPIO22
//  Sensor: IR/Light sensor module — DO=GPIO5
//          LOW  = bright room (light hits sensor freely)
//          HIGH = dark room OR finger covering sensor
//
//  SENSOR BEHAVIOUR SUMMARY
//  ─────────────────────────────────────────────────────────
//  The sensor shares double duty:
//    • Short HIGH pulse (<TAP_MAX_MS) released = tap
//    • Long HIGH held  (>TAP_MAX_MS) while room bright = pet
//    • Sustained HIGH for DARK_SLEEP_TRIGGER_MS = dark room → sleep
//
//  DARK SLEEP
//  ─────────────────────────────────────────────────────────
//  • Room goes dark (HIGH sustained ≥ 7 s) → dog sleeps
//  • Every 30 s the sensor is sampled:
//      LOW  → wakes up for 20 s (shine a torch at sensor to wake)
//      HIGH → stays asleep, resets 30 s timer
//  • After the 20 s awake window, if still dark → back to sleep
//  • If light returns permanently → normal operation resumes
//
//  RANDOM BEHAVIOURS
//  ─────────────────────────────────────────────────────────
//  Sleep is NOT a random behaviour — only dark-sleep triggers it.
//  Weighted random pool (per BEHAVIOR_MS interval):
//    WALK  60%  (dog walks left/right, tail wags)
//    EAT   20%  (dog walks to bowl, eats, bowl slides out)
//    TALK  20%  (speech bubble, woof + mood emoji)
//
//  INTERACTIONS
//  ─────────────────────────────────────────────────────────
//  • Single tap / pet  → heart eyes, floating hearts
//  • Double-tap        → dog jumps twice with hearts
//  • Pet while sleeping→ dog briefly lifts head, heart eyes
//  • Pet while eating  → pauses nod, heart eyes
//
//  All timing values are #defines at the top — easy to tweak.
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ╔══════════════════════════════════════════════════════════╗
// ║              EASY TWEAK — ALL TIMINGS HERE              ║
// ╠══════════════════════════════════════════════════════════╣

// ── frame & animation ──────────────────────────────────────
#define FRAME_MS                33    // ms per frame  (~30 fps)
#define ANIM_MS                160    // walking leg animation tick
#define TAIL_MS                110    // tail wag tick
#define SUN_RAY_MS             400    // sun ray twinkle tick
#define STAR_TWINKLE_MS        400    // star twinkle tick

// ── behaviour timing ───────────────────────────────────────
#define BEHAVIOR_MS          10000    // ms before next random behaviour
#define WALK_SPEED            1.2f    // pixels per frame (walking)

// ── eating ─────────────────────────────────────────────────
#define NOD_MS                 300    // eating head-nod tick
#define EAT_DURATION          4000    // ms eating before bowl slides out
#define BOWL_SLIDE_SPEED      2.8f    // pixels/frame bowl exits screen

// ── sleeping ───────────────────────────────────────────────
#define ZZZ_SPAWN_MS          1400    // ms between ZZZ spawns
#define SLEEP_SIT_MS          1200    // ms dog sits before lying down
#define SLEEP_REACT_MS        1000    // ms dog lifts head when petted asleep
#define SLEEP_BACK_MS          600    // ms settle after head-lift

// ── interaction ────────────────────────────────────────────
#define TAP_MAX_MS             600    // HIGH pulse ≤ this = tap; longer = pet
#define DOUBLE_TAP_MS          500    // max gap between two taps for double-tap
#define PET_REACT_MS          1200    // ms heart-eyes last after pet/tap
#define PET_MOOD_MS          15000    // ms "recently petted" happy mood lasts

// ── jump ───────────────────────────────────────────────────
#define JUMP_HEIGHT             18    // peak pixels dog rises
#define JUMP_DURATION          500    // ms per jump arc

// ── dark sleep ─────────────────────────────────────────────
#define DARK_SLEEP_TRIGGER_MS 7000    // ms sustained HIGH → dark sleep
#define DARK_CHECK_MS        30000    // ms between light checks while sleeping
#define DARK_WAKE_MS         20000    // ms dog stays awake after flashlight wake

// ── behaviour weights (must sum to 100) ────────────────────
#define WALK_WEIGHT             60    // % chance of WALK
#define EAT_WEIGHT              20    // % chance of EAT
// TALK gets the remainder (100 - WALK_WEIGHT - EAT_WEIGHT)

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
enum Behavior  { WALK, EAT, TALK };           // SLEEP excluded from random pool
enum EatPhase  { EAT_WALK_TO, EAT_EATING, EAT_BOWL_OUT };
enum SleepPhase{ SLEEP_SIT, SLEEP_LIE, SLEEP_LIFTING, SLEEP_BACK };

// ────────────────────────────────────────────────────────────
//  STATE
// ────────────────────────────────────────────────────────────

// ── behaviour ───────────────────────────────────────────────
Behavior   behavior      = WALK;
EatPhase   eatPhase      = EAT_WALK_TO;
SleepPhase sleepPhase    = SLEEP_SIT;
bool       isSleeping    = false;     // true whenever dog is in sleep mode

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

// ── animation counters ──────────────────────────────────────
int animFrame   = 0;
int tailFrame   = 0;
int sunRayFrame = 0;
int starFrame   = 0;
int eatNodFrame = 0;
int woofIdx     = 0;
int jumpCount   = 0;

// ── dog position ────────────────────────────────────────────
float dogX    = 40.0f;
int   dogY    = 48;           // ground baseline (feet)
int   dogDir  = 1;            // 1 = right, -1 = left
float bowlX   = 110.0f;
int   bowlSide= 1;

// ── interaction flags ───────────────────────────────────────
bool isPetted       = false;
bool petReacting    = false;
bool eatPaused      = false;
bool sleepReacting  = false;
bool talkVisible    = true;
bool isJumping      = false;
bool recentlyPetted = false;

unsigned long petMoodTimer = 0;

// ── dark sleep ──────────────────────────────────────────────
// darkSleeping  : true while forced dark-sleep is active
// highSince     : when current continuous HIGH streak started
// darkCheckTimer: timestamp of last 30s check
// darkWakeTimer : when the 20s flashlight-wake window started
// darkWaking    : true during the 20s temporary wake window
bool          darkSleeping  = false;
bool          darkWaking    = false;
unsigned long highSince     = 0;
unsigned long darkCheckTimer= 0;
unsigned long darkWakeTimer = 0;

// ── tap / pet detection ─────────────────────────────────────
// Edge detection uses lastPinState (updated at END of frame).
// A tap is a HIGH pulse that ends within TAP_MAX_MS.
// A pet is a HIGH pulse held longer than TAP_MAX_MS.
// Double-tap: two taps whose falling edges are within DOUBLE_TAP_MS.
bool          lastPinState   = false;
unsigned long highStartTime  = 0;     // when current HIGH pulse began
unsigned long lastTapRelease = 0;     // falling edge time of last tap
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
  {"WOOF!", ":)" },   // 0  happy (post-pet)
  {"ARF!",  "<3" },   // 1  happy (post-pet)
  {"WOOF?", ".." },   // 2  neutral
  {"ARF!",  ":D" },   // 3  neutral
  {"WOOF!", ":o" },   // 4  surprised
  {"ARF?",  ":/" },   // 5  grumpy
};

// ────────────────────────────────────────────────────────────
//  WEIGHTED RANDOM BEHAVIOUR
//  SLEEP is excluded — only dark sensor triggers it.
//  cur = current behaviour to avoid immediate repeat.
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
//  cx, by    = center x, feet y (ground baseline)
//  dir       = 1 right / -1 left
//  frame     = leg anim frame 0/1
//  sleeping  = flat lying pose
//  sitting   = sitting pose
//  heartEyes = heart eyes override
//  tailWag   = 0/1
//  nodOffset = head bob (eating)
//  jumpOff   = vertical offset (negative = up, jump arc)
// ────────────────────────────────────────────────────────────
void drawDog(int cx, int by, int dir, int frame,
             bool sleeping, bool sitting, bool heartEyes,
             int tailWag, int nodOffset, int jumpOff) {

  int drawBy = by + jumpOff;

  // ── tail ──────────────────────────────────────────────────
  int tbx = cx + dir * (-7);
  int tby = drawBy - 12;
  if (sleeping) {
    display.drawLine(cx+6, drawBy-2, cx+10, drawBy-3, WHITE);
    display.drawPixel(cx+11, drawBy-4, WHITE);
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

  // ── body ──────────────────────────────────────────────────
  if (sleeping) {
    display.fillRoundRect(cx-11, drawBy-8, 22, 8, 3, WHITE);
  } else if (sitting) {
    display.fillRoundRect(cx-7, drawBy-14, 14, 14, 4, WHITE);
  } else {
    display.fillRoundRect(cx-8, drawBy-16, 16, 13, 3, WHITE);
  }

  // ── legs ──────────────────────────────────────────────────
  if (!sleeping) {
    if (sitting) {
      display.fillRect(cx-4, drawBy-5, 3, 5, WHITE);
      display.fillRect(cx+1, drawBy-5, 3, 5, WHITE);
    } else {
      if (frame == 0) {
        display.fillRect(cx-7, drawBy-6, 3, 6, WHITE);
        display.fillRect(cx+4, drawBy-6, 3, 6, WHITE);
      } else {
        display.fillRect(cx-7, drawBy-4, 3, 4, WHITE);
        display.fillRect(cx+5, drawBy-8, 3, 8, WHITE);
        display.fillRect(cx-6, drawBy-8, 3, 8, WHITE);
        display.fillRect(cx+4, drawBy-4, 3, 4, WHITE);
      }
    }
  }

  // ── head ──────────────────────────────────────────────────
  int headX = cx + dir * 5;
  int headW = 13, headH = 11;
  int headY = sleeping ? (drawBy - 10 + nodOffset) : (drawBy - 22 + nodOffset);

  display.fillRoundRect(headX - headW/2, headY - headH/2, headW, headH, 3, WHITE);

  // snout
  int snoutX = headX + dir * 4;
  display.fillRoundRect(snoutX - 2, headY, 5, 4, 1, WHITE);
  display.fillRect(snoutX - 1, headY, 3, 2, BLACK);

  // ── eyes ──────────────────────────────────────────────────
  int eyeX = headX + dir * 1;
  int eyeY = headY - 1;

  if (heartEyes) {
    drawHeart(eyeX - 3, eyeY - 2);
    drawHeart(eyeX + 1, eyeY - 2);
  } else if (sleeping) {
    // closed arc eyes
    display.drawPixel(eyeX-1, eyeY+1, BLACK);
    display.drawPixel(eyeX,   eyeY,   BLACK);
    display.drawPixel(eyeX+1, eyeY,   BLACK);
    display.drawPixel(eyeX+3, eyeY+1, BLACK);
    display.drawPixel(eyeX+4, eyeY,   BLACK);
    display.drawPixel(eyeX+5, eyeY,   BLACK);
  } else {
    display.fillRect(eyeX - 1, eyeY - 1, 2, 2, BLACK);
    display.fillRect(eyeX + 3, eyeY - 1, 2, 2, BLACK);
    display.drawPixel(eyeX,     eyeY - 1, WHITE);
    display.drawPixel(eyeX + 4, eyeY - 1, WHITE);
  }

  // ── beagle ears ───────────────────────────────────────────
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

  if (b == EAT) {
    eatPhase = EAT_WALK_TO;
    bowlSide = (random(0,2) == 0) ? 1 : -1;
    bowlX    = (bowlSide == 1) ? 110.0f : 18.0f;
    dogDir   = bowlSide;
  }
  if (b == TALK) {
    woofIdx    = recentlyPetted ? random(0,2) : random(2,6);
    talkTimer  = now;
    talkVisible= true;
  }
}

// Enter dark-sleep: skip sit intro, go straight to lying.
// Called when room has been dark for DARK_SLEEP_TRIGGER_MS.
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
  zzzTimer       = now - ZZZ_SPAWN_MS;   // first ZZZ spawns immediately
  for (int i = 0; i < MAX_HEARTS; i++) hearts[i].life = 0;
}

// Exit dark-sleep: called when light detected during 30s check.
// Starts a 20s temporary wake window.
void exitDarkSleep() {
  darkSleeping  = false;
  darkWaking    = true;
  darkWakeTimer = now;
  isSleeping    = false;
  // clear ZZZs
  for (int i = 0; i < MAX_ZZZ; i++) zzzs[i].life = 0;
  startBehavior(WALK);
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

  highSince    = millis();
  lastPinState = (digitalRead(SENSOR_PIN) == HIGH);

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
  //  STEP 1 — READ SENSOR & COMPUTE EDGES
  //  Edges are computed against lastPinState which was set
  //  at the END of the previous frame — no same-frame races.
  // ════════════════════════════════════════════════════════
  bool pinHigh     = (digitalRead(SENSOR_PIN) == HIGH);
  bool risingEdge  = pinHigh  && !lastPinState;
  bool fallingEdge = !pinHigh && lastPinState;

  // Track continuous HIGH duration for dark-sleep detection.
  if (risingEdge)  highSince = now;          // new HIGH streak starts
  if (fallingEdge) highSince = now;          // reset — streak broken by LOW

  // ════════════════════════════════════════════════════════
  //  STEP 2 — DARK SLEEP STATE MACHINE
  //
  //  States:
  //    !darkSleeping && !darkWaking  → normal operation
  //    darkSleeping                  → asleep, check every 30s
  //    darkWaking                    → awake for 20s after flashlight
  // ════════════════════════════════════════════════════════

  // --- normal → dark sleep trigger ---
  if (!darkSleeping && !darkWaking) {
    if (pinHigh && (now - highSince >= DARK_SLEEP_TRIGGER_MS)) {
      enterDarkSleep();
    }
  }

  // --- while dark sleeping: 30s check ---
  if (darkSleeping) {
    if (now - darkCheckTimer >= DARK_CHECK_MS) {
      darkCheckTimer = now;
      if (!pinHigh) {
        // Light detected (flashlight) → temp wake for 20s
        exitDarkSleep();
      }
      // still dark → stay sleeping, timer resets automatically
    }
    // Skip ALL interaction & behaviour logic while dark-sleeping.
    // Jump to anim ticks and draw below.
    goto ANIM_AND_DRAW;
  }

  // --- dark wake window: 20s of normal life ---
  if (darkWaking) {
    if (now - darkWakeTimer >= DARK_WAKE_MS) {
      // Wake window expired — check current light level
      if (pinHigh) {
        // Still dark → go back to sleep
        enterDarkSleep();
        goto ANIM_AND_DRAW;
      } else {
        // Light is back permanently → resume normal operation
        darkWaking = false;
      }
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 3 — TAP / PET LOGIC
  //  Only reached when NOT dark-sleeping.
  //  Room is bright (or temporarily woken) so HIGH pulses
  //  are finger interactions, not ambient darkness.
  // ════════════════════════════════════════════════════════
  {
    isPetted     = pinHigh;
    doubleTapped = false;

    // Rising edge: finger just placed
    if (risingEdge) {
      highStartTime = now;

      // Fire pet reaction immediately on finger-down
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

    // Falling edge: finger lifted
    if (fallingEdge) {
      unsigned long pulseDuration = now - highStartTime;

      if (pulseDuration < TAP_MAX_MS) {
        // It was a tap — check for double-tap
        unsigned long gap = now - lastTapRelease;
        if (gap < DOUBLE_TAP_MS && gap > 30) {
          doubleTapped = true;
        }
        lastTapRelease = now;
      }

      petReacting = false;
      if (!isSleeping && behavior == EAT) eatPaused = false;
    }

    // Double-tap → jump (overrides pet reaction)
    if (doubleTapped && !isJumping && !isSleeping) {
      petReacting   = false;
      isJumping     = true;
      jumpCount     = 0;
      jumpTimer     = now;
      behavior      = WALK;
      behaviorTimer = now;
      for (int i = 0; i < 5; i++) spawnHeart(dogX, dogY - 30);
    }

    // Mood and pet-react timeouts
    if (recentlyPetted && now - petMoodTimer > PET_MOOD_MS) recentlyPetted = false;
    if (petReacting && !isPetted && now - petStart > PET_REACT_MS) petReacting = false;
  }

  // ════════════════════════════════════════════════════════
  //  STEP 4 — BEHAVIOUR TIMER
  //  Weighted random: WALK 60%, EAT 20%, TALK 20%
  //  SLEEP never chosen here — only via dark sensor.
  // ════════════════════════════════════════════════════════
  if (!isJumping && !isSleeping && now - behaviorTimer > BEHAVIOR_MS) {
    startBehavior(weightedRandomBehavior(behavior));
  }

  // ════════════════════════════════════════════════════════
  //  ANIM_AND_DRAW label — dark-sleeping jumps here
  // ════════════════════════════════════════════════════════
  ANIM_AND_DRAW:

  // ── anim ticks ──────────────────────────────────────────
  if (now - animTimer > ANIM_MS)         { animFrame   = (animFrame+1)%2;   animTimer  = now; }
  if (now - tailTimer > TAIL_MS)         { tailFrame   = (tailFrame+1)%2;   tailTimer  = now; }
  if (now - sunTimer  > SUN_RAY_MS)      { sunRayFrame = (sunRayFrame+1)%2; sunTimer   = now; }
  if (now - starTimer > STAR_TWINKLE_MS) { starFrame   = (starFrame+1)%2;   starTimer  = now; }

  // ── float hearts ────────────────────────────────────────
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].life > 0) { hearts[i].y -= 0.55f; hearts[i].life--; }
  }
  // Continuous hearts while actively petting (not dark)
  if (isPetted && !darkSleeping && random(0,6) == 0) {
    spawnHeart(dogX + dogDir*5, dogY - 28);
  }

  // ── float ZZZs ──────────────────────────────────────────
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzs[i].life > 0) {
      zzzs[i].y -= 0.3f;
      zzzs[i].x += 0.2f;
      zzzs[i].life--;
    }
  }

  // ── jump arc ────────────────────────────────────────────
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
      jumpTimer = now;
      jumpCount++;
      if (jumpCount >= 2) { isJumping = false; jumpOffset = 0; }
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 5 — BEHAVIOUR UPDATE
  // ════════════════════════════════════════════════════════
  bool heartEyes = false;
  bool sleeping  = false;
  bool sitting   = false;
  int  nodOffset = 0;

  if (isSleeping || darkSleeping) {
    // ── sleep rendering ───────────────────────────────────
    sleeping = true;

    if (sleepPhase == SLEEP_SIT) {
      sitting  = true;
      sleeping = false;
      if (now - sleepPhaseTimer > SLEEP_SIT_MS) {
        sleepPhase      = SLEEP_LIE;
        sleepPhaseTimer = now;
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
      sitting    = true;
      sleeping   = false;
      heartEyes  = true;
      if (now - sleepReactTimer > SLEEP_REACT_MS) {
        sleepPhase      = SLEEP_BACK;
        sleepPhaseTimer = now;
      }
    }
    if (sleepPhase == SLEEP_BACK) {
      sitting  = true;
      sleeping = false;
      if (now - sleepPhaseTimer > SLEEP_BACK_MS) {
        sleepPhase    = SLEEP_LIE;
        sleepReacting = false;
      }
    }

  } else {

    switch (behavior) {

      case WALK: {
        if (!isPetted && !isJumping) {
          dogX += dogDir * WALK_SPEED;
          if (dogX > 110) dogDir = -1;
          if (dogX <  18) dogDir =  1;
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
            eatPhase     = EAT_EATING;
            eatNodTimer  = now;
            eatDoneTimer = now;
            dogDir = bowlSide;
          }
        }
        if (eatPhase == EAT_EATING) {
          if (!eatPaused) {
            if (now - eatNodTimer > NOD_MS) {
              eatNodFrame = (eatNodFrame+1) % 2;
              eatNodTimer = now;
            }
            nodOffset = (eatNodFrame == 1) ? 3 : 0;
            if (now - eatDoneTimer > EAT_DURATION) eatPhase = EAT_BOWL_OUT;
          } else {
            heartEyes = true;
            nodOffset = 0;
          }
        }
        if (eatPhase == EAT_BOWL_OUT) {
          bowlX += bowlSide * BOWL_SLIDE_SPEED;
          if (bowlX < -20 || bowlX > 148) {
            startBehavior(weightedRandomBehavior(EAT));
          }
        }
        break;
      }

      case TALK: {
        heartEyes = isPetted && petReacting;
        if (now - talkTimer > 2200) {
          talkTimer   = now;
          talkVisible = !talkVisible;
          if (talkVisible) {
            woofIdx = recentlyPetted ? random(0,2) : random(2,6);
          }
        }
        break;
      }
    }
  }

  // ════════════════════════════════════════════════════════
  //  STEP 6 — DRAW
  // ════════════════════════════════════════════════════════
  display.clearDisplay();

  // background: night when sleeping or talking
  bool nightScene = isSleeping || darkSleeping || behavior == TALK;
  if (nightScene) {
    drawMoon(110, 10);
    drawStars(starFrame);
  } else {
    drawSun(12, 10, sunRayFrame);
  }

  // ground line
  display.drawLine(0, dogY+1, 127, dogY+1, WHITE);

  // bowl (eat only)
  if (!isSleeping && !darkSleeping && behavior == EAT) {
    drawBowl((int)bowlX, dogY);
  }

  // ZZZs (sleep only)
  if (isSleeping || darkSleeping) drawZzzs();

  // speech bubble (talk only)
  if (!isSleeping && !darkSleeping && behavior == TALK && talkVisible && !isPetted) {
    drawSpeechBubble((int)dogX, dogY - 26,
                     woofPool[woofIdx].word,
                     woofPool[woofIdx].emoji);
  }

  // dog
  drawDog((int)dogX, dogY, dogDir, animFrame,
          sleeping, sitting, heartEyes,
          tailFrame, nodOffset, jumpOffset);

  // hearts
  for (int i = 0; i < MAX_HEARTS; i++) {
    if (hearts[i].life > 0) {
      drawHeart((int)hearts[i].x, (int)hearts[i].y, hearts[i].life > 28);
    }
  }

  display.display();

  // ── update edge-detection state for next frame ───────────
  lastPinState = pinHigh;
}
