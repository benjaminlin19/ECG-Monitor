#include <Wire.h>
#include <U8g2lib.h>
#include "MAX30105.h"

// ---------- Hardware ----------
#define SDA_PIN 4            // D2
#define SCL_PIN 5            // D1
#define FINGER_THRESHOLD 10000

MAX30105 sensor;
// 2.42" SSD1309 128x64. If yours is SSD1306, use U8G2_SSD1306_128X64_NONAME_F_HW_I2C
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------- Graph ----------
const int GRAPH_W      = 128;
const int GRAPH_TOP    = 18;
const int GRAPH_BOTTOM = 62;
const int GRAPH_MID    = (GRAPH_TOP + GRAPH_BOTTOM) / 2;   // 40
const int GRAPH_HALF   = (GRAPH_BOTTOM - GRAPH_TOP) / 2;   // 22
uint8_t graphY[GRAPH_W];        // y pixel for each column (circular buffer)
int graphHead = 0;              // next write position
byte graphDecimate = 0;

// ---------- Filter state ----------
float dcIR = 0, dcRed = 0;      // slow baselines (DC)
float msIR = 0, msRed = 0;      // running mean-square of the ripple (AC)
float fIR = 0, fRed = 0;        // lightly smoothed AC
float prev1 = 0, prev2 = 0;     // for peak detection

// ---------- Beat tracking ----------
unsigned long lastBeat = 0;
const int N = 5;
float intervals[N];
int idx = 0, count = 0;

float spo2Smooth = 0;
int dispBpm = 0, dispSpo2 = 0;
unsigned long lastCalc = 0, lastDraw = 0;
bool fingerOn = false;

void clearGraph() {
  for (int i = 0; i < GRAPH_W; i++) graphY[i] = GRAPH_MID;
  graphHead = 0;
  graphDecimate = 0;
}

void resetState() {
  dcIR = dcRed = msIR = msRed = fIR = fRed = prev1 = prev2 = 0;
  lastBeat = 0;
  idx = count = 0;
  spo2Smooth = 0;
  dispBpm = dispSpo2 = 0;
  clearGraph();
}

void showMessage(const char* l1, const char* l2) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tr);
  u8g2.drawStr(0, 28, l1);
  u8g2.drawStr(0, 48, l2);
  u8g2.sendBuffer();
}

void drawScreen() {
  u8g2.clearBuffer();

  // ----- Header: BPM and SpO2 at the top right -----
  char hdr[24];
  if (fingerOn && dispBpm > 0) snprintf(hdr, sizeof(hdr), "%d bpm %d%%", dispBpm, dispSpo2);
  else                         snprintf(hdr, sizeof(hdr), "-- bpm --%%");
  u8g2.setFont(u8g2_font_7x13B_tr);
  int w = u8g2.getStrWidth(hdr);
  u8g2.drawStr(128 - w, 12, hdr);

  // Status at the top left
  u8g2.setFont(u8g2_font_6x10_tr);
  if (fingerOn && dispBpm == 0) u8g2.drawStr(0, 11, "Measuring");

  u8g2.drawHLine(0, 15, 128);

  // ----- Graph area -----
  if (!fingerOn) {
    u8g2.setFont(u8g2_font_ncenB08_tr);//CHANGE: Font
    const char* msg = "NO SIGNAL";
    int mw = u8g2.getStrWidth(msg);
    u8g2.drawStr((128 - mw) / 2, 44, msg);
  } else {
    int prevY = graphY[graphHead % GRAPH_W];
    for (int i = 0; i < GRAPH_W; i++) {
      int y = graphY[(graphHead + i) % GRAPH_W];
      if (i > 0) u8g2.drawLine(i - 1, prevY, i, y);
      prevY = y;
    }
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  u8g2.begin();
  showMessage("Starting...", "");

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    showMessage("MAX30102", "not found");
    while (1) delay(10);
  }

  // brightness, avg, mode, sampleRate, pulseWidth, adcRange (200/4 = 50 samples/sec)
  sensor.setup(30, 4, 2, 200, 411, 4096);

  clearGraph();
}

void loop() {
  sensor.check();

  while (sensor.available()) {
    float ir  = sensor.getFIFOIR();
    float red = sensor.getFIFORed();
    sensor.nextSample();
    unsigned long now = millis();

    // No finger: clear everything and skip processing
    if (ir < FINGER_THRESHOLD) {
      if (fingerOn) { resetState(); fingerOn = false; }
      continue;
    }
    fingerOn = true;

    // Remove baseline (DC), keep ripple (AC)
    if (dcIR == 0) { dcIR = ir; dcRed = red; }
    dcIR  = 0.98 * dcIR  + 0.02 * ir;
    dcRed = 0.98 * dcRed + 0.02 * red;
    float acIR  = ir  - dcIR;
    float acRed = red - dcRed;

    // Light smoothing
    fIR  = 0.7 * fIR  + 0.3 * acIR;
    fRed = 0.7 * fRed + 0.3 * acRed;

    // Running RMS of the ripple (about 2 seconds of memory)
    msIR  = 0.99 * msIR  + 0.01 * fIR  * fIR;
    msRed = 0.99 * msRed + 0.01 * fRed * fRed;
    float rmsIR = sqrt(msIR);

    // Inverted so each heartbeat points UP (blood absorbs light, so raw IR dips on a beat)
    float sig = -fIR;

    // Peak detection on the previous sample
    bool isPeak = (prev1 > prev2) && (prev1 >= sig) && (prev1 > 0.7 * rmsIR) && (rmsIR > 5);
    prev2 = prev1;
    prev1 = sig;

    if (isPeak) {
      unsigned long dt = now - lastBeat;
      if (lastBeat != 0 && dt > 350 && dt < 1500) {   // 40 to 170 BPM
        intervals[idx] = dt;
        idx = (idx + 1) % N;
        if (count < N) count++;
      }
      if (lastBeat == 0 || dt > 350) lastBeat = now;
    }

    // Add a point to the graph every 2nd sample (about 25 points per second)
    if (++graphDecimate >= 2) {
      graphDecimate = 0;
      float scale = 2.0 * rmsIR;
      if (scale < 10) scale = 10;
      int y = GRAPH_MID - (int)((sig / scale) * GRAPH_HALF);
      if (y < GRAPH_TOP)    y = GRAPH_TOP;
      if (y > GRAPH_BOTTOM) y = GRAPH_BOTTOM;
      graphY[graphHead] = y;
      graphHead = (graphHead + 1) % GRAPH_W;
    }

    // SpO2 from the ratio of ratios
    if (rmsIR > 5 && msRed > 0) {
      float R = (sqrt(msRed) / dcRed) / (rmsIR / dcIR);
      float s = 110.0 - 25.0 * R;
      if (s > 100) s = 100;
      if (s < 70)  s = 70;
      spo2Smooth = (spo2Smooth == 0) ? s : 0.98 * spo2Smooth + 0.02 * s;
    }

    // Recalculate displayed values once per second
    if (now - lastCalc > 1000) {
      lastCalc = now;
      if (count >= 3) {
        float sum = 0;
        for (int i = 0; i < count; i++) sum += intervals[i];
        dispBpm  = (int)(60000.0 / (sum / count) + 0.5);
        dispSpo2 = (int)(spo2Smooth + 0.5);
      } else {
        dispBpm = dispSpo2 = 0;
      }
      Serial.print("BPM: ");   Serial.print(dispBpm);
      Serial.print("  SpO2: "); Serial.print(dispSpo2);
      Serial.print("%  IR=");   Serial.println((long)ir);
    }
  }

  // Redraw the screen about 16 times per second
  unsigned long t = millis();
  if (t - lastDraw >= 60) {
    lastDraw = t;
    drawScreen();
  }

  yield();
}