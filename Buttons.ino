unsigned long debounceDelay = 50;
unsigned long lastDebounceTime = 0;

const unsigned long REPEAT_DELAY = 400;
const unsigned long REPEAT_RATE  = 150;

int state = 0;
unsigned long pressStart = 0;
unsigned long lastRepeatTime = 0;

String getButtonAction() {
  unsigned long now = millis();

  if (now - lastDebounceTime < debounceDelay) {
    return "";
  }
  lastDebounceTime = now;

  bool upPressed   = (digitalRead(BTN_UP)   == LOW);
  bool downPressed = (digitalRead(BTN_DOWN) == LOW);

  if (upPressed && downPressed) {
    if (state != 3) {
      state = 3;
      pressStart = now;
      lastRepeatTime = now;
    }
    unsigned long bothDuration = now - pressStart;
    if (bothDuration >= 10000) {
      if (state != 4) {
        state = 4;
        Serial.println("UNLOCKED (both >10s)");
        return "unlocked";
      }
    }
    return "";
  }
  if (state == 3 || state == 4) {
    unsigned long bothDuration = now - pressStart;
    state = 0;

    if (bothDuration >= 10000) {
      Serial.println("UNLOCKED (released)");
      return "unlocked";
    } else if (bothDuration >= 1000) {
      Serial.println("EXIT (both 1-10s)");
      return "exit";
    } else {
      Serial.println("SELECT (both <1s)");
      return "select";
    }
  }
  if (upPressed && !downPressed) {
    if (state != 1) {
      state = 1;
      pressStart = now;
      lastRepeatTime = now;
      Serial.println("UP");
      return "up";
    } else {
      if (now - pressStart > REPEAT_DELAY && now - lastRepeatTime >= REPEAT_RATE) {
        lastRepeatTime = now;
        Serial.println("UP (repeat)");
        return "up";
      }
    }
    return "";
  }
  if (downPressed && !upPressed) {
    if (state != 2) {
      state = 2;
      pressStart = now;
      lastRepeatTime = now;
      Serial.println("DOWN");
      return "down";
    } else {
      if (now - pressStart > REPEAT_DELAY && now - lastRepeatTime >= REPEAT_RATE) {
        lastRepeatTime = now;
        Serial.println("DOWN (repeat)");
        return "down";
      }
    }
    return "";
  }
  if (!upPressed && !downPressed) {
    state = 0;
  }
  return "";
}