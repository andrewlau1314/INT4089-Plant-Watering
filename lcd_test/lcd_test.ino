#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

hd44780_I2Cexp lcd(0x27);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("LCD hardware test");
  Wire.begin(21, 22);
  Wire.setClock(100000);

  int status = lcd.begin(16, 2);
  Serial.print("begin status=");
  Serial.println(status);
  if (status) {
    Serial.println("Trying auto-detect constructor fallback...");
  }

  lcd.backlight();
  lcd.display();
  lcd.clear();
  lcd.home();
  lcd.print("HELLO ESP32");
  lcd.setCursor(0, 1);
  lcd.print("LINE2 OK?");
  Serial.println("Wrote HELLO ESP32 / LINE2 OK?");
  Serial.println("If blank: turn blue contrast pot on backpack");
}

void loop() {
  static unsigned long last;
  if (millis() - last < 1000) return;
  last = millis();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("HELLO ESP32    ");
  lcd.setCursor(0, 1);
  lcd.print("t=");
  lcd.print(millis() / 1000);
  lcd.print("s          ");
  Serial.print("refresh t=");
  Serial.println(millis() / 1000);
}
