#include <WiFi.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <Wire.h>

// ---------------- BUTTON PINOUT ----------------
#define BTN_LOW 4
#define BTN_NORMAL 16
#define BTN_HIGH 15
#define BTN_IP 17
#define BTN_ERROR 5

// ---------------- LED PINOUT ----------------
#define LED_LOW 14
#define LED_HIGH 27
#define LED_FAIL 26

// ---------------- BUZZER ----------------
#define BUZZER 23

// ---------------- OLED ----------------
// Change U8G2_R0 to U8G2_R2 to rotate display 180 degrees
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);

// ---------------- WIFI ----------------
const char* ssid = "TPMS";
const char* password = "87654321";
WebServer server(80);

// ---------------- VARIABLES ----------------
int pressureValue = 32;
bool sensorFail = false;
bool manualError = false;
bool showIP = false;
unsigned long ipShownAt = 0;

// Limits
#define LOW_LIMIT 25
#define HIGH_LIMIT 55

// Preloader
bool showPreloader = true;
unsigned long preloaderStart = 0;
int preloaderProgress = 0;

// OLED Animation
int progressBar = 0;
bool progressForward = true;

// LED & buzzer timers
unsigned long ledTimer = 0;
unsigned long buzzerTimer = 0;
int ledLevel = 10;
bool ledIncreasing = true;
bool buzzerState = false;

// Button debounce
unsigned long lastDebounceTime[5] = {0, 0, 0, 0, 0};
bool lastButtonState[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
bool buttonState[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
bool buttonPressed[5] = {false, false, false, false, false};
#define DEBOUNCE_DELAY 50

// Error button double-click
unsigned long errorLastPress = 0;
int errorPressCount = 0;

// Simulation mode: 0 = NORMAL, 1 = LOW (leak), 2 = HIGH (overpressure)
int currentMode = 0;

// Timers for simulations
unsigned long lastNormalSim = 0;
unsigned long lastLowSim = 0;
unsigned long lastHighSim = 0;

// WiFi connection status
bool wifiConnected = false;

// ---------------- API ENDPOINT ----------------
void handleData() {
  String json = "{";
  json += "\"pressure\":" + String(pressureValue) + ",";
  json += "\"status\":\"";
  if (sensorFail) {
    json += "SENSOR FAILURE";
  } else if (pressureValue < LOW_LIMIT) {
    json += "LOW PRESSURE";
  } else if (pressureValue > HIGH_LIMIT) {
    json += "HIGH PRESSURE";
  } else {
    json += "NORMAL";
  }
  json += "\",";
  json += "\"sensorFail\":" + String(sensorFail ? "true" : "false") + ",";
  json += "\"lowLimit\":" + String(LOW_LIMIT) + ",";
  json += "\"highLimit\":" + String(HIGH_LIMIT) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- WEB PAGE ----------------
String buildPage() {
  String p = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>TPMS</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;color:#fff}.container{background:rgba(255,255,255,0.1);backdrop-filter:blur(10px);border-radius:20px;padding:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3);max-width:400px;width:90%}h1{text-align:center;margin-bottom:10px;font-size:28px}h2{text-align:center;margin-bottom:20px;font-size:14px;opacity:0.9;font-weight:normal}.pressure-display{background:rgba(255,255,255,0.2);border-radius:15px;padding:25px;margin-bottom:20px;text-align:center}.pressure-value{font-size:48px;font-weight:bold;margin-bottom:5px}.pressure-unit{font-size:18px;opacity:0.8}.status{padding:12px;border-radius:10px;margin-bottom:20px;text-align:center;font-weight:bold;font-size:16px}.status.normal{background:rgba(76,175,80,0.3)}.status.low{background:rgba(255,152,0,0.3)}.status.high{background:rgba(244,67,54,0.3)}.status.error{background:rgba(158,158,158,0.3)}.controls{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:20px}.btn{background:rgba(255,255,255,0.2);border:2px solid rgba(255,255,255,0.3);color:#fff;padding:12px;border-radius:10px;cursor:pointer;font-size:14px;transition:all 0.3s}.btn:hover{background:rgba(255,255,255,0.3);transform:translateY(-2px)}.btn:active{transform:translateY(0)}.info{background:rgba(255,255,255,0.1);border-radius:10px;padding:15px;font-size:14px}.info-row{display:flex;justify-content:space-between;margin-bottom:8px}.info-row:last-child{margin-bottom:0}.label{opacity:0.8}.value{font-weight:bold}</style></head><body><div class="container"><h1>🚗 TIRE PRESSURE MONITORING SYSTEM</h1><div class="pressure-display"><div class="pressure-value" id="pressure">32</div><div class="pressure-unit">PSI</div></div><div class="status normal" id="status">NORMAL</div><div class="controls"><button class="btn" onclick="setMode('low')">⬇️ Low<br>25</button><button class="btn" onclick="setMode('normal')">✓ Normal<br>32</button><button class="btn" onclick="setMode('high')">⬆️ High<br>55</button></div><div class="info"><div class="info-row"><span class="label">🌐 Connected IP:</span><span class="value" id="ip">...</span></div><div class="info-row"><span class="label">📡 Live</span><span class="value" id="updated">Connecting...</span></div></div></div><script>let ws;function fetchData(){fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('pressure').textContent=d.pressure;document.getElementById('ip').textContent=d.ip;let st='';let cls='';if(d.sensorFail){st='SENSOR FAILURE';cls='error'}else if(d.pressure<d.lowLimit){st='LOW PRESSURE';cls='low'}else if(d.pressure>d.highLimit){st='HIGH PRESSURE';cls='high'}else{st='NORMAL';cls='normal'}let el=document.getElementById('status');el.textContent=st;el.className='status '+cls;document.getElementById('updated').textContent=new Date().toLocaleTimeString()}).catch(e=>console.error(e))}function setMode(m){console.log('Mode:',m)}setInterval(fetchData,1000);fetchData()</script></body></html>)rawliteral";
  return p;
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

// ---------------- BUTTON READ ----------------
bool readButton(int pin, int index) {
  bool reading = digitalRead(pin);
  if (reading != lastButtonState[index]) {
    lastDebounceTime[index] = millis();
  }
  if ((millis() - lastDebounceTime[index]) > DEBOUNCE_DELAY) {
    if (reading != buttonState[index]) {
      buttonState[index] = reading;
      if (buttonState[index] == LOW && !buttonPressed[index]) {
        buttonPressed[index] = true;
        lastButtonState[index] = reading;
        return true;
      } else if (buttonState[index] == HIGH) {
        buttonPressed[index] = false;
      }
    }
  }
  lastButtonState[index] = reading;
  return false;
}

// ---------------- SIMULATIONS ----------------
void simulateNormal() {
  unsigned long now = millis();
  if (now - lastNormalSim > 2500) {
    lastNormalSim = now;
    int change = random(-1, 2);
    pressureValue += change;
    if (pressureValue < 28) pressureValue = 28;
    if (pressureValue > 35) pressureValue = 35;
  }
}

void simulateLow() {
  unsigned long now = millis();
  if (now - lastLowSim > 2000) {
    lastLowSim = now;
    int drop = random(0, 3);
    pressureValue -= drop;
    if (pressureValue < 18) pressureValue = 18;
  }
}

void simulateHigh() {
  unsigned long now = millis();
  if (now - lastHighSim > 1500) {
    lastHighSim = now;
    int change = random(-2, 3);
    pressureValue += change;
    if (pressureValue < 55) pressureValue = 55;
    if (pressureValue > 64) pressureValue = 64;
  }
}

// ---------------- DRAW GAUGE ----------------
void drawPressureGauge(int x, int y, int pressure) {
  // Draw gauge arc background
  for (int i = 0; i < 5; i++) {
    oled.drawCircle(x, y + 10, 22 - i, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  }
  
  // Calculate needle angle based on pressure (10-70 PSI range)
  int angle = map(constrain(pressure, 10, 70), 10, 70, 180, 0);
  float rad = angle * 3.14159 / 180.0;
  int needleX = x + (int)(18 * cos(rad));
  int needleY = y + 10 - (int)(18 * sin(rad));
  
  // Draw needle
  oled.drawLine(x, y + 10, needleX, needleY);
  oled.drawDisc(x, y + 10, 3);
  
  // Draw markers
  oled.setFont(u8g2_font_micro_tr);
  oled.drawStr(x - 22, y + 15, "L");
  oled.drawStr(x + 18, y + 15, "H");
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  oled.begin();
  randomSeed((unsigned long)micros());
  
  pinMode(BTN_LOW, INPUT_PULLUP);
  pinMode(BTN_NORMAL, INPUT_PULLUP);
  pinMode(BTN_HIGH, INPUT_PULLUP);
  pinMode(BTN_IP, INPUT_PULLUP);
  pinMode(BTN_ERROR, INPUT_PULLUP);
  
  pinMode(LED_LOW, OUTPUT);
  pinMode(LED_HIGH, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  // Setup LED PWM
  const int freq = 5000;
  const int resolution = 8;
  ledcSetup(0, freq, resolution);
  ledcAttachPin(LED_LOW, 0);
  ledcSetup(1, freq, resolution);
  ledcAttachPin(LED_HIGH, 1);
  ledcSetup(2, freq, resolution);
  ledcAttachPin(LED_FAIL, 2);
  
  preloaderStart = millis();
  
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(100);
  }
  
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  
  Serial.println("TPMS Started");
  if (wifiConnected) {
    Serial.println("IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi not connected");
  }
}

// ---------------- LOOP ----------------
void loop() {
  server.handleClient();
  
  // PRELOADER (3 seconds)
  if (showPreloader) {
    unsigned long elapsed = millis() - preloaderStart;
    if (elapsed < 3000) {
      preloaderProgress = map(elapsed, 0, 3000, 0, 100);
      
      oled.clearBuffer();
      oled.setFont(u8g2_font_fub14_tf);
      oled.drawStr(25, 25, "TPMS");
      oled.setFont(u8g2_font_6x10_tr);
      oled.drawStr(10, 40, "Tire Pressure");
      oled.drawStr(5, 50, "Monitoring System");
      
      // Progress bar
      oled.drawFrame(14, 54, 100, 8);
      oled.drawBox(15, 55, preloaderProgress - 2, 6);
      
      oled.sendBuffer();
      return;
    } else {
      showPreloader = false;
    }
  }
  
  // READ BUTTONS
  if (readButton(BTN_ERROR, 4)) {
    unsigned long now = millis();
    if (now - errorLastPress < 500) {
      errorPressCount++;
    } else {
      errorPressCount = 1;
    }
    errorLastPress = now;
    
    if (errorPressCount == 1) {
      sensorFail = true;
      manualError = true;
      Serial.println("Error ON");
    }
    if (errorPressCount >= 2) {
      sensorFail = false;
      manualError = false;
      pressureValue = 32;
      currentMode = 0;
      errorPressCount = 0;
      Serial.println("Error OFF - Reset");
    }
  }
  
  if (!sensorFail) {
    if (readButton(BTN_LOW, 0)) {
      pressureValue = random(20, 24);
      currentMode = 1;
      Serial.println("Mode: LOW " + String(pressureValue));
    }
    if (readButton(BTN_NORMAL, 1)) {
      pressureValue = random(30, 34);
      currentMode = 0;
      Serial.println("Mode: NORMAL " + String(pressureValue));
    }
    if (readButton(BTN_HIGH, 2)) {
      pressureValue = random(56, 60);
      currentMode = 2;
      Serial.println("Mode: HIGH " + String(pressureValue));
    }
  }
  
  if (readButton(BTN_IP, 3)) {
    showIP = true;
    ipShownAt = millis();
    Serial.println("IP: " + WiFi.localIP().toString());
  }
  
  // RUN SIMULATION
  if (!sensorFail) {
    if (currentMode == 0) simulateNormal();
    else if (currentMode == 1) simulateLow();
    else if (currentMode == 2) simulateHigh();
  }
  
  // LED BREATHING
  unsigned long now = millis();
  if (now - ledTimer > 10) {
    ledTimer = now;
    if (ledIncreasing) {
      ledLevel++;
      if (ledLevel >= 230) ledIncreasing = false;
    } else {
      ledLevel--;
      if (ledLevel <= 10) ledIncreasing = true;
    }
  }
  
  int pwm = ledLevel;
  if (sensorFail) {
    ledcWrite(2, pwm);
    ledcWrite(0, 0);
    ledcWrite(1, 0);
  } else if (pressureValue < LOW_LIMIT) {
    ledcWrite(0, pwm);
    ledcWrite(1, 0);
    ledcWrite(2, 0);
  } else if (pressureValue > HIGH_LIMIT) {
    ledcWrite(1, pwm);
    ledcWrite(0, 0);
    ledcWrite(2, 0);
  } else {
    ledcWrite(0, pwm / 3);
    ledcWrite(1, 0);
    ledcWrite(2, 0);
  }
  
  // BUZZER - FIXED WITH PROPER TONE GENERATION
  if (now - buzzerTimer > 800) {
    buzzerTimer = now;
    buzzerState = !buzzerState;
    
    if (buzzerState) {
      if (sensorFail) {
        tone(BUZZER, 800, 200);
      } else if (pressureValue < LOW_LIMIT) {
        tone(BUZZER, 600, 150);
      } else if (pressureValue > HIGH_LIMIT) {
        tone(BUZZER, 1000, 150);
      }
    } else {
      noTone(BUZZER);
    }
  }
  
  // ============ OLED DISPLAY ============
  oled.clearBuffer();
  
  if (showIP && millis() - ipShownAt < 4000) {
    // IP DISPLAY
    oled.setFont(u8g2_font_fub11_tf);
    oled.drawStr(5, 15, "IP ADDRESS");
    oled.drawFrame(0, 0, 128, 64);
    
    oled.setFont(u8g2_font_7x13_tr);
    String ip = wifiConnected ? WiFi.localIP().toString() : "Not Connected";
    int ipWidth = oled.getStrWidth(ip.c_str());
    oled.drawStr((128 - ipWidth) / 2, 35, ip.c_str());
    
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(15, 55, "Press IP to exit");
    
  } else {
    showIP = false;
    
    if (sensorFail) {
      // ERROR DISPLAY
      oled.drawFrame(0, 0, 128, 64);
      oled.setFont(u8g2_font_fub14_tf);
      oled.drawStr(10, 20, "SENSOR");
      oled.drawStr(10, 38, "FAILURE!");
      
      // Error icon
      oled.drawFrame(90, 10, 30, 30);
      oled.drawLine(95, 15, 115, 35);
      oled.drawLine(115, 15, 95, 35);
      
      oled.setFont(u8g2_font_6x10_tr);
      oled.drawStr(5, 55, "Check sensor");
      
    } else {
      // NORMAL DISPLAY WITH GAUGE
      oled.drawFrame(0, 0, 128, 64);
      
      // Title
      oled.setFont(u8g2_font_fub11_tf);
      oled.drawStr(5, 13, "TPMS");
      
      // WiFi status icon
      oled.setFont(u8g2_font_5x7_tr);
      if (wifiConnected) {
        oled.drawStr(100, 10, "WiFi");
      } else {
        oled.drawStr(95, 10, "NoWiFi");
      }
      
      // Draw gauge
      drawPressureGauge(30, 22, pressureValue);
      
      // Pressure value
      oled.setFont(u8g2_font_fub14_tf);
      String psi = String(pressureValue);
      oled.drawStr(70, 35, psi.c_str());
      oled.setFont(u8g2_font_7x13_tr);
      oled.drawStr(100, 35, "PSI");
      
      // Status bar at bottom
      oled.drawLine(0, 45, 128, 45);
      oled.setFont(u8g2_font_7x13_tf);
      
      if (pressureValue < LOW_LIMIT) {
        oled.drawStr(3, 58, "LOW PRESSURE");
        // Low indicator bar
        int barWidth = map(pressureValue, 10, LOW_LIMIT, 0, 40);
        oled.drawBox(90, 50, constrain(barWidth, 0, 40), 8);
      } else if (pressureValue > HIGH_LIMIT) {
        oled.drawStr(3, 58, "HIGH ALERT!");
        // High indicator bar (full)
        oled.drawBox(90, 50, 40, 8);
      } else {
        oled.drawStr(3, 58, "NORMAL");
        // Normal indicator bar (medium)
        int barWidth = map(pressureValue, LOW_LIMIT, HIGH_LIMIT, 15, 35);
        oled.drawBox(90, 50, constrain(barWidth, 15, 35), 8);
      }
      oled.drawFrame(89, 49, 42, 10);
    }
  }
  
  oled.sendBuffer();
}