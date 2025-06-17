#include <Wire.h>
#include <millisDelay.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "TFT_eSPI.h"
#include <ChainableLED.h>

// 硬件定义
TFT_eSPI tft = TFT_eSPI();
RTC_SAMD51 rtc;

// Grove Chainable RGB LED
#define LED_CLK_PIN A0    // 时钟引脚
#define LED_DATA_PIN A1   // 数据引脚
#define NUM_LEDS 1        // LED数量
ChainableLED leds(LED_CLK_PIN, LED_DATA_PIN, NUM_LEDS);

// 自定义颜色
#define TFT_GRAY 0x7BEF

#define BUZZER_PIN WIO_BUZZER
#define BACKLIGHT_PIN LCD_BACKLIGHT
#define BUTTON_A WIO_KEY_A
#define BUTTON_B WIO_KEY_B
#define BUTTON_C WIO_KEY_C

// 系统状态
bool rtcInitialized = false;

// 时间设置模式
enum TimeMode {
  NORMAL_MODE,
  SET_HOUR_MODE,
  SET_MINUTE_MODE,
  SET_ALARM_HOUR_MODE,
  SET_ALARM_MINUTE_MODE
};

TimeMode currentMode = NORMAL_MODE;
unsigned long modeStartTime = 0;
const unsigned long MODE_TIMEOUT = 10000; // 10秒后自动退出设置模式

// 闹钟时间
int alarmHour = 7;
int alarmMinute = 30;
bool alarmTriggered = false;
bool alarmEnabled = true;

// 音乐
int length = 15;
char notes[] = "ccggaagffeeddc ";
int beats[] = { 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2, 4 };
int tempo = 300;

void setup() {
  Serial.begin(115200);
  
  // 初始化屏幕
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  
  // 显示启动信息
  displayStatus("Initializing...");
  delay(1000);

  // 初始化背光和按键
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  // 初始化LED
  displayStatus("Initializing LED...");
  leds.init();
  
  // LED测试 - 快速显示红绿蓝
  leds.setColorRGB(0, 255, 0, 0); // 红色
  delay(300);
  leds.setColorRGB(0, 0, 255, 0); // 绿色
  delay(300);
  leds.setColorRGB(0, 0, 0, 255); // 蓝色
  delay(300);
  leds.setColorRGB(0, 0, 0, 0);   // 关闭
  
  Serial.println("Chainable RGB LED initialized and tested");

  // 初始化RTC
  displayStatus("Starting RTC...");
  Serial.println("Attempting to initialize RTC...");
  
  if (rtc.begin()) {
    rtcInitialized = true;
    displayStatus("RTC OK");
    Serial.println("RTC initialized successfully");
    
    // 设置编译时间作为初始时间
    DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
    rtc.adjust(compileTime);
    
    Serial.printf("Compile time: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  compileTime.year(), compileTime.month(), compileTime.day(),
                  compileTime.hour(), compileTime.minute(), compileTime.second());
    
    DateTime currentTime = rtc.now();
    Serial.printf("RTC time set to: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  currentTime.year(), currentTime.month(), currentTime.day(),
                  currentTime.hour(), currentTime.minute(), currentTime.second());
    
    displayStatus("Time set to compile time");
  } else {
    rtcInitialized = false;
    displayStatus("RTC Failed!");
    Serial.println("RTC initialization failed!");
    delay(2000);
  }
  
  delay(2000);
  
  // 显示使用说明
  showInstructions();
  delay(3000);
  
  // 清屏准备显示时钟
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  // 检查模式超时
  if (currentMode != NORMAL_MODE && millis() - modeStartTime > MODE_TIMEOUT) {
    currentMode = NORMAL_MODE;
  }
  
  // LED状态指示
  static unsigned long lastLedUpdate = 0;
  if (millis() - lastLedUpdate > 3000) { // 每3秒更新一次
    if (currentMode == NORMAL_MODE) {
      // 正常模式：绿色微光表示系统正常
      leds.setColorRGB(0, 0, 10, 0);
    } else {
      // 设置模式：蓝色微光
      leds.setColorRGB(0, 0, 0, 10);
    }
    lastLedUpdate = millis();
  }
  
  if (rtcInitialized) {
    DateTime now = rtc.now();
    
    // 每秒更新一次显示
    static int lastSecond = -1;
    if (now.second() != lastSecond) {
      lastSecond = now.second();
      displayTime(now);
    }
    
    // 检查闹钟（仅在正常模式下）
    if (currentMode == NORMAL_MODE) {
      checkAlarm(now);
    }
  } else {
    // 如果RTC失败，显示系统时间
    displaySystemTime();
  }
  
  handleButtons();
  delay(50); // 减少CPU使用率
}

void checkAlarm(DateTime now) {
  if (alarmEnabled && !alarmTriggered && 
      now.hour() == alarmHour && now.minute() == alarmMinute && now.second() < 5) {
    alarmTriggered = true;
    triggerAlarm();
  }
  
  // 重置闹钟状态
  if (now.minute() != alarmMinute) {
    alarmTriggered = false;
  }
}

void triggerAlarm() {
  Serial.println("Alarm triggered!");
  
  // 显示闹钟信息
  tft.fillRect(0, 200, 320, 40, TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 210);
  tft.println("ALARM! Press any key to stop");
  
  unsigned long alarmStartTime = millis();
  const unsigned long alarmDuration = 20000; // 20秒
  
  // 音乐和LED变量
  int noteIndex = 0;
  unsigned long noteStartTime = millis();
  unsigned long ledToggleTime = millis();
  bool ledState = false;
  bool isPlayingNote = false;
  
  while (millis() - alarmStartTime < alarmDuration) {
    // 检查是否按下任意按键来停止闹钟
    if (digitalRead(BUTTON_A) == LOW || 
        digitalRead(BUTTON_B) == LOW || 
        digitalRead(BUTTON_C) == LOW) {
      Serial.println("Alarm stopped by button press");
      break;
    }
    
    // LED闪烁控制（每500ms切换一次）
    if (millis() - ledToggleTime >= 500) {
      ledState = !ledState;
      if (ledState) {
        // 红色闪烁
        leds.setColorRGB(0, 255, 0, 0);
      } else {
        // 关闭
        leds.setColorRGB(0, 0, 0, 0);
      }
      ledToggleTime = millis();
    }
    
    // 音乐播放控制
    if (!isPlayingNote) {
      // 开始播放新音符
      if (noteIndex < length) {
        if (notes[noteIndex] == ' ') {
          // 暂停
          noTone(BUZZER_PIN);
        } else {
          // 播放音符
          int freq = 0;
          switch (notes[noteIndex]) {
            case 'c': freq = 261; break;
            case 'd': freq = 294; break;
            case 'e': freq = 329; break;
            case 'f': freq = 349; break;
            case 'g': freq = 392; break;
            case 'a': freq = 440; break;
            case 'b': freq = 493; break;
          }
          if (freq > 0) {
            tone(BUZZER_PIN, freq);
          }
        }
        isPlayingNote = true;
        noteStartTime = millis();
      } else {
        // 重新开始播放
        noteIndex = 0;
      }
    } else {
      // 检查当前音符是否播放完毕
      if (millis() - noteStartTime >= beats[noteIndex] * tempo) {
        noTone(BUZZER_PIN);
        isPlayingNote = false;
        noteIndex++;
        if (noteIndex >= length) {
          noteIndex = 0; // 循环播放
        }
      }
    }
    
    delay(50); // 减少CPU使用率
  }
  
  // 停止所有声音和LED
  noTone(BUZZER_PIN);
  leds.setColorRGB(0, 0, 0, 0); // 关闭LED
  
  // 清除闹钟显示
  tft.fillRect(0, 200, 320, 40, TFT_BLACK);
  
  Serial.println("Alarm finished");
}

void displayTime(DateTime now) {
  // 清除时间显示区域
  tft.fillRect(0, 30, 320, 150, TFT_BLACK);
  
  // 显示当前时间（根据模式高亮显示）
  tft.setTextSize(4);
  if (currentMode == SET_HOUR_MODE) {
    tft.setTextColor(TFT_YELLOW);
  } else {
    tft.setTextColor(TFT_CYAN);
  }
  tft.setCursor(30, 40);
  tft.printf("%02d", now.hour());
  
  tft.setTextColor(TFT_WHITE);
  tft.printf(":");
  
  if (currentMode == SET_MINUTE_MODE) {
    tft.setTextColor(TFT_YELLOW);
  } else {
    tft.setTextColor(TFT_CYAN);
  }
  tft.printf("%02d", now.minute());
  
  tft.setTextColor(TFT_WHITE);
  tft.printf(":");
  tft.setTextColor(TFT_CYAN);
  tft.printf("%02d", now.second());
  
  // 显示日期
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(30, 90);
  tft.printf("%04d-%02d-%02d", now.year(), now.month(), now.day());
  
  // 显示星期
  const char* daysOfWeek[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  tft.setCursor(200, 90);
  tft.printf("%s", daysOfWeek[now.dayOfTheWeek()]);
  
  // 显示闹钟设置（根据模式高亮显示）
  tft.setCursor(30, 120);
  tft.print("Alarm: ");
  
  if (currentMode == SET_ALARM_HOUR_MODE) {
    tft.setTextColor(TFT_YELLOW);
  } else {
    tft.setTextColor(alarmEnabled ? TFT_GREEN : TFT_GRAY);
  }
  tft.printf("%02d", alarmHour);
  
  tft.setTextColor(alarmEnabled ? TFT_GREEN : TFT_GRAY);
  tft.printf(":");
  
  if (currentMode == SET_ALARM_MINUTE_MODE) {
    tft.setTextColor(TFT_YELLOW);
  } else {
    tft.setTextColor(alarmEnabled ? TFT_GREEN : TFT_GRAY);
  }
  tft.printf("%02d", alarmMinute);
  
  tft.setTextColor(alarmEnabled ? TFT_GREEN : TFT_GRAY);
  tft.printf(" %s", alarmEnabled ? "ON" : "OFF");
  
  // 显示当前模式和操作提示
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, 190);
  
  switch (currentMode) {
    case NORMAL_MODE:
      tft.printf("A:Set Time | B:Set Alarm | C:Toggle Alarm");
      break;
    case SET_HOUR_MODE:
      tft.printf("Setting Hour - A:+1 | B:Confirm | C:Cancel");
      break;
    case SET_MINUTE_MODE:
      tft.printf("Setting Minute - A:+1 | B:Confirm | C:Cancel");
      break;
    case SET_ALARM_HOUR_MODE:
      tft.printf("Setting Alarm Hour - A:+1 | B:Confirm | C:Cancel");
      break;
    case SET_ALARM_MINUTE_MODE:
      tft.printf("Setting Alarm Minute - A:+1 | B:Confirm | C:Cancel");
      break;
  }
  
  // 显示在线状态和硬件状态
  tft.setCursor(10, 210);
  tft.setTextColor(TFT_WHITE);
  if (rtcInitialized) {
    tft.printf("RTC:OK | LED:A0/A1 | Alarm:20s");
  } else {
    tft.printf("RTC:FAILED | LED:A0/A1 | Alarm:20s");
  }
}

void displaySystemTime() {
  unsigned long currentTime = millis() / 1000;
  int hours = (currentTime / 3600) % 24;
  int minutes = (currentTime % 3600) / 60;
  int seconds = currentTime % 60;
  
  tft.fillRect(0, 30, 320, 60, TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(TFT_RED);
  tft.setCursor(30, 40);
  tft.printf("%02d:%02d:%02d", hours, minutes, seconds);
  tft.setTextSize(2);
  tft.setCursor(30, 90);
  tft.printf("(System Time - RTC Failed)");
}

void displayStatus(const char* message) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 10);
  tft.println("Wio Terminal Clock");
  tft.setCursor(10, 50);
  tft.println(message);
  Serial.println(message);
}

void showInstructions() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 10);
  tft.println("Instructions:");
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 40);
  tft.println("Normal Mode:");
  tft.println("  A: Enter time setting");
  tft.println("  B: Enter alarm setting");
  tft.println("  C: Toggle alarm on/off");
  tft.println("");
  tft.println("Setting Mode:");
  tft.println("  A: Increase value");
  tft.println("  B: Confirm & next/finish");
  tft.println("  C: Cancel");
  tft.println("");
  tft.println("Alarm: 20 seconds with LED flash");
  tft.println("LED: Connect to A0(CLK) & A1(DATA)");
  tft.println("");
  tft.setTextColor(TFT_YELLOW);
  tft.println("Install ChainableLED library first!");
}

void handleButtons() {
  static unsigned long lastDebounce = 0;
  if (millis() - lastDebounce < 200) return; // 减少防抖动时间
  
  if (digitalRead(BUTTON_A) == LOW) {
    tone(BUZZER_PIN, 1000, 50); // 按键音效反馈
    handleButtonA();
    lastDebounce = millis();
    Serial.println("Button A pressed");
  }
  
  if (digitalRead(BUTTON_B) == LOW) {
    tone(BUZZER_PIN, 1200, 50); // 按键音效反馈
    handleButtonB();
    lastDebounce = millis();
    Serial.println("Button B pressed");
  }
  
  if (digitalRead(BUTTON_C) == LOW) {
    tone(BUZZER_PIN, 800, 50); // 按键音效反馈
    handleButtonC();
    lastDebounce = millis();
    Serial.println("Button C pressed");
  }
}

void handleButtonA() {
  Serial.printf("Button A pressed in mode: %d\n", currentMode);
  
  switch (currentMode) {
    case NORMAL_MODE:
      // 进入时间设置模式
      currentMode = SET_HOUR_MODE;
      modeStartTime = millis();
      Serial.println("Entering time setting mode");
      break;
      
    case SET_HOUR_MODE:
      // 增加小时
      if (rtcInitialized) {
        DateTime now = rtc.now();
        int newHour = (now.hour() + 1) % 24;
        // 创建新的DateTime对象并调整RTC
        DateTime newTime = DateTime(now.year(), now.month(), now.day(), newHour, now.minute(), 0);
        rtc.adjust(newTime);
        Serial.printf("Hour increased to: %d\n", newHour);
      } else {
        Serial.println("RTC not initialized, cannot set time");
      }
      break;
      
    case SET_MINUTE_MODE:
      // 增加分钟
      if (rtcInitialized) {
        DateTime now = rtc.now();
        int newMinute = (now.minute() + 1) % 60;
        // 创建新的DateTime对象并调整RTC
        DateTime newTime = DateTime(now.year(), now.month(), now.day(), now.hour(), newMinute, 0);
        rtc.adjust(newTime);
        Serial.printf("Minute increased to: %d\n", newMinute);
      } else {
        Serial.println("RTC not initialized, cannot set time");
      }
      break;
      
    case SET_ALARM_HOUR_MODE:
      // 增加闹钟小时
      alarmHour = (alarmHour + 1) % 24;
      Serial.printf("Alarm hour increased to: %d\n", alarmHour);
      break;
      
    case SET_ALARM_MINUTE_MODE:
      // 增加闹钟分钟
      alarmMinute = (alarmMinute + 1) % 60;
      Serial.printf("Alarm minute increased to: %d\n", alarmMinute);
      break;
  }
}

void handleButtonB() {
  Serial.printf("Button B pressed in mode: %d\n", currentMode);
  
  switch (currentMode) {
    case NORMAL_MODE:
      // 进入闹钟设置模式
      currentMode = SET_ALARM_HOUR_MODE;
      modeStartTime = millis();
      Serial.println("Entering alarm setting mode");
      break;
      
    case SET_HOUR_MODE:
      // 确认小时，进入分钟设置
      currentMode = SET_MINUTE_MODE;
      modeStartTime = millis();
      Serial.println("Now setting minutes");
      break;
      
    case SET_MINUTE_MODE:
      // 确认分钟，完成时间设置
      currentMode = NORMAL_MODE;
      Serial.println("Time setting completed");
      break;
      
    case SET_ALARM_HOUR_MODE:
      // 确认闹钟小时，进入闹钟分钟设置
      currentMode = SET_ALARM_MINUTE_MODE;
      modeStartTime = millis();
      Serial.println("Now setting alarm minutes");
      break;
      
    case SET_ALARM_MINUTE_MODE:
      // 确认闹钟分钟，完成闹钟设置
      currentMode = NORMAL_MODE;
      Serial.println("Alarm setting completed");
      break;
  }
}

void handleButtonC() {
  Serial.printf("Button C pressed in mode: %d\n", currentMode);
  
  switch (currentMode) {
    case NORMAL_MODE:
      // 切换闹钟开关
      alarmEnabled = !alarmEnabled;
      Serial.printf("Alarm %s\n", alarmEnabled ? "enabled" : "disabled");
      break;
      
    default:
      // 取消设置，返回正常模式
      currentMode = NORMAL_MODE;
      Serial.println("Setting cancelled");
      break;
  }
}