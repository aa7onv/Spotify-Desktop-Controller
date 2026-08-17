/*******************************************************************
    Hello World for the ESP32 CYD
/*******************************************************************/

#include <TFT_eSPI.h>


TFT_eSPI tft = TFT_eSPI();


void setup() {
  // Start  tft display 
  tft.init();
  tft.setRotation(1); //set display to landscape

  // Clear the screen before writing to it
  tft.fillScreen(TFT_BLACK);

  int x = 320 / 2;
  int y = 10;
  int fontNum = 2;

  // Center "Hello"
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Hello", x, y, fontNum);

  // Center "World"
  y += 16;
  tft.setTextColor(0x001F, TFT_BLACK); //blue
  tft.drawCentreString("World", x, y, fontNum);
}


void loop() {
  // put your main code here, to run repeatedly:

}
