#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PID_v1.h>
#include "InterpolationLib.h"
#include <Fonts/Org_01.h>
#include "anim.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

#define temp 7
#define res1 4
#define res2 5
#define plate 9
#define buzzer 8

#define BTN_UP   6
#define BTN_DOWN 13

const double R0 = 100000.0;
const double T0 = 25.0 + 273.15;
const double BETA = 3950.0;
const unsigned long RES1 = 1000;
const unsigned long RES2 = 100000;
double RES = RES1;
static unsigned long lastSwitchTime = 0;
const unsigned long switchCooldown = 500;
const int numValues = 21;
//double tHot[numValues] = { 20.2, 25, 40, 60.2, 80.2, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240 };
//double errorValues[numValues] = { 2.50,  0.50,  0.07, -0.20, -2.10, -2.40, -3.40, -5.07, -3.80, -5.53, -8.53, -9.05, -11.33, -11.75, -15.75, -18.40, -22.24, -24.48, -26.87, -30.75, -31.45 };

unsigned long lastMillis = 0;
const int samples = 40;
int s = 0;
const int thr = 110;
bool wRes = false;
double sumTemp = 0.0;
double sumRes = 0.0;
double avgTemp = 0.0;
double lastTemp = 0.0;
double avgRes = 0.0;
bool finished = false;
int safeTemp = 40;
int heatingState = 0;
int heatingProgress = 0;
bool heated = false;

double setPoint = 0, output = 0;
int mode = 0;
bool menu = false;
unsigned long previousMillis = 0;
int currentFrame = 0;
String modes[3] = {"HEAT", "LEAD", "NOLEAD"};
int modesSize = 3;
int menuPos = 0;
int curveTemp1[] = {145, 165, 225};
int curveTime1[] = {0, 70, 20};
int curveOffset1[] = {10, 0, 10};
int curveTemp2[] = {145, 180, 245};
int curveTime2[] = {0, 60, 15};
int curveOffset2[] = {10, 0, 10};
int arraySize = 3;
int curveStep = 0;
unsigned long holdStart = 0;
bool holding = false;
int targetTemp = 0;
int targetOffset = 0;
bool done = false;
bool ex = false;
bool buz = false;

//pid
double Kp = 4;
double Ki = 0.001;
double Kd = 90;

PID myPID(&avgTemp, &output, &setPoint, Kp, Ki, Kd, DIRECT);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(9600);
  pinMode(temp, INPUT);
  pinMode(res1, OUTPUT);
  pinMode(res2, OUTPUT);
  pinMode(buzzer, OUTPUT);
  digitalWrite(res1, HIGH);
  digitalWrite(res2, LOW);

  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  delay(500);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.setRotation(2);
  display.clearDisplay();
  display.display();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(0);
  display.setFont(&Org_01);
  introAnimation();

  myPID.SetOutputLimits(0, 255);
  myPID.SetSampleTime(1000);
  myPID.SetMode(AUTOMATIC);
}

void loop() {
  unsigned long nowMillis = millis();

  if(buz) {
    digitalWrite(buzzer, HIGH);
    delay(200);
    digitalWrite(buzzer, LOW);
    buz = false;
  }

  //Button 
  String btn = getButtonAction();
  if(mode == 0 && menu == false) {
    if(btn == "up" || btn == "down" || btn == "select") { menu = true; btn = ""; }
  } 
  else if(mode > 0 && menu == false) {
    if(btn == "select") { menu = true; btn = ""; } 
    else if(btn == "exit") { setPoint = 0; targetOffset = 0; heatingState = 1; ex = true; curveStep = 0; }
    if(mode == 1 && menu == false) {
      if(btn == "up") { setPoint += 5; if (setPoint > 250) setPoint = 250; }
      else if(btn == "down") { setPoint -= 5; if (setPoint < 0) setPoint = 0; }
    }
  }

  if(mode > 0 && menu == false) {
    if(avgTemp > 40) heated = true;
    if(avgTemp < setPoint) heatingState = 2;
    else if(avgTemp >= setPoint && heated) heatingState = 1;
    else heatingState = 0;
    if((heated || ex) && avgTemp <= 40) { setPoint = 0; mode = 0; heatingState = 0; heated = false; done = false; holdStart = 0; ex = false; targetOffset = 0; }
  }

  if(menu == true) {
    if(btn == "select") { 
      if(mode != menuPos+1) { 
        setPoint = 0; 
        heated = false;  
        holding = false;
        curveStep = 0; 
        heatingProgress = 0; 
        targetOffset = 0; 
        done = false;
        holdStart = 0;
        ex = false;
        targetOffset = 0;
      } 
      mode = menuPos+1; 
      btn = "exit"; 
    }
    if(btn == "exit") { menu = false; menuPos = 0; }
    else if(btn == "up") { menuPos++; if(menuPos >= modesSize) { menuPos = 0; }}
    else if(btn == "down") { menuPos--; if(menuPos < 0) { menuPos = modesSize-1; }}
  } 

  if (mode > 1 && !done && !ex) {
    int curveTime = 0;
    if(mode == 2) {
      targetTemp = curveTemp1[curveStep];
      targetOffset = curveOffset1[curveStep];
      curveTime = curveTime1[curveStep];
    } else if(mode == 3) {
      targetTemp = curveTemp2[curveStep];
      targetOffset = curveOffset2[curveStep];
      curveTime = curveTime2[curveStep];
    }
    if (!holding) {
      setPoint = targetTemp+targetOffset;
      if (avgTemp >= targetTemp-2) {
        holding = true;
        holdStart = nowMillis;
      }
    }
    else {
      unsigned long holdTime = (unsigned long)curveTime * 1000UL;
      if (nowMillis - holdStart >= holdTime) {
        curveStep++;
        buz = true;
        holding = false;
        if (curveStep >= arraySize) {
          setPoint = 0;
          heatingState = 1;
          curveStep = 0;
          targetOffset = 0;
          done = true;
        }
      }
    }
    heatingProgress = map(curveStep, 0, arraySize, 0, 128);
  }

  static unsigned long lastPrint = 0;
  if (nowMillis - lastPrint >= 5000) {
    lastPrint = nowMillis;
    Serial.print(avgTemp, 1);
    Serial.print(",");
    Serial.println(setPoint, 1);
  }

  if(s < samples) {
    int adcValue = analogRead(temp);
    if (adcValue == 0) adcValue = 1;
    double resistance = RES * ((1023.0 / adcValue) - 1.0);
    double tempK = 1.0 / ( (1.0 / BETA) * log(resistance / R0) + (1.0 / T0) );
    double tempC = tempK - 273.15;
  
    sumTemp += tempC;
    sumRes += resistance;
    s++;
    delayMicroseconds(1000);
  }else finished = true;

  if (finished) {
    avgTemp = sumTemp / samples;
    avgRes  = sumRes / samples;
    if(lastTemp == 0) lastTemp = avgTemp;
    //if(avgTemp < tHot[0]) avgTemp += -0.152028912254266 * avgTemp + 9.81035163281709;
    //else avgTemp += Interpolation::ConstrainedSpline(tHot, errorValues, numValues, avgTemp);

    display.clearDisplay();
    if(menu == false) drawTemp(avgTemp, setPoint-targetOffset, mode, heatingState, heatingProgress);
    else drawMenu();
    display.display();

    sumTemp = 0.0;
    sumRes  = 0.0;
    s = 0;
    finished = false;

    if (nowMillis - lastSwitchTime > switchCooldown) {
      if (avgTemp > (thr + 4) && wRes) {
        digitalWrite(res1, HIGH);
        digitalWrite(res2, LOW);
        RES = RES1; wRes = false;
        lastSwitchTime = nowMillis;
      } else if (avgTemp < (thr - 4) && !wRes) {
        digitalWrite(res1, LOW);
        digitalWrite(res2, HIGH);
        RES = RES2; wRes = true;
        lastSwitchTime = nowMillis;
      }
    }
  }
  if(avgTemp < -50 || analogRead(temp)==0 || lastTemp-avgTemp>10 || lastTemp-avgTemp<-10) {
    display.clearDisplay();
    if(avgTemp < -50) {
      display.setTextSize(5);
      display.setCursor(5, 23);
      display.print("WIRE");
    } else {
      display.setTextSize(3);
      display.setCursor(12, 20);
      display.print("BROKEN");
    }
    display.display();
    for(;;); 
  }
  lastTemp = avgTemp;
  myPID.Compute();
  if (avgTemp > setPoint + 0.5) {
    myPID.SetMode(MANUAL);
    output = 0;
  } else myPID.SetMode(AUTOMATIC);
  analogWrite(plate, (int)output);
}

void drawTemp(double t, double setT, double Mode, int State, int HeatingProgress) {
  static int circleSize = 0;
  if(Mode == 0) { // idle
    display.setTextSize(4);
    display.setCursor(0, 20);
    display.print(String(t,1));
  } else if (Mode > 0) {
    display.setTextSize(3);
    display.setCursor(0, 14);
    display.print(String(t,1));
    display.setTextSize(2);
    display.setCursor(0, 28);
    display.print(String(setT,1));
    unsigned long currentMillis = millis();
    if(State == 1) { //cooling
      if (currentMillis - previousMillis >= FRAME_DELAY) {
        previousMillis = currentMillis;
        display.drawBitmap(HOURGLASS_X, HOURGLASS_Y, frames[currentFrame], FRAME_WIDTH, FRAME_HEIGHT, WHITE);
        currentFrame = (currentFrame + 1) % FRAME_COUNT;
      } 
    } else if(State == 2) { //heating
      if (currentMillis - previousMillis >= FRAME_DELAY) {
        previousMillis = currentMillis;
        display.drawBitmap(HOURGLASS_X, HOURGLASS_Y, frames1[currentFrame], FRAME_WIDTH, FRAME_HEIGHT, WHITE);
        currentFrame = (currentFrame + 1) % FRAME_COUNT1;
      }
    }
    if(Mode > 1) {
      display.drawLine(0, 31, HeatingProgress, 31, 1);
      display.fillRoundRect(HeatingProgress-10, 28, 12, 7, 2, 0);
      display.fillRoundRect(HeatingProgress-9, 29, 10, 6, 2, 1);
    }
  }
}
void drawMenu() {
  for(int i = 2;i<32;i+=2) display.writePixel(124, i, 1);
  display.fillRect(123, 32/modesSize*(menuPos), 3, 10, 1);
  display.setTextSize(3);
  display.setCursor(10, 20);
  display.print(modes[menuPos]);
  display.drawLine(5, 27, 5, 3, 1);
}
void introAnimation() {
  display.clearDisplay();
  randomSeed(millis());

  const int SIZE = 4;
  const int COLS = 128 / SIZE;
  const int ROWS = 32  / SIZE;
  int pos = 0;
  int m = 0;

  while (pos <= 150) {
    for(int j = 0; j<4;j++) {
      int col = random(COLS);
      int row = random(ROWS);
      int x = col * SIZE;
      int y = row * SIZE;
      if (display.getPixel(x + SIZE/2, y + SIZE/2) == 0) {
        display.fillRect(x+1, y+1, SIZE-1, SIZE-1, WHITE);
      }
    }
    m++;
    if(m > 30) {
      for(int i = 0; i<pos; i++) {
        int inv_i = 128-i;
        display.drawLine(inv_i+8, 0, inv_i, 16, 0);
        display.drawLine(inv_i+8, 32, inv_i, 16, 0);
      }
      pos+=3;
      int real_pos = 132-pos;
      for(int k = 0;k<3;k++) {
        display.drawLine(real_pos+8+k, 0, real_pos+k, 16, 1);
        display.drawLine(real_pos+8+k, 32, real_pos+k, 16, 1);
      }
      for(int k = 0;k<1;k++) {
        display.drawLine(real_pos+15+k, 3, real_pos+8+k, 16, 1);
        display.drawLine(real_pos+15+k, 29, real_pos+8+k, 16, 1);
      }
    }
    display.display();
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 19);
  display.print("HOT PLATE");
  display.display();
  delay(800);
  display.clearDisplay();
  display.display();
}
