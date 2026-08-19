/*adjustable vars for func / performance
setJpgScale()
COLOR_SAMPLE_MAX
SPI_Frequency in User_Setup.h

*/
#include <TJpg_Decoder.h> // jpeg decoder library
#include <FS.h>
#include <SPIFFS.h>        // file system for ESP32 flash memory
#include <ArduinoJson.h>
#include <base64.h>

// WiFi and http client
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
#include "List_SPIFFS.h"
#include "Web_Fetch.h"
#include "index.h"

// Include the TFT library https://github.com/Bodmer/TFT_eSPI
#include "SPI.h"
#include <TFT_eSPI.h>
// NOTE: TFT_eSPI must be configured (via its User_Setup.h, or a User_Setup_Select.h
// entry) for specific ESP32-CYD board's display pins. 
 // A common config for the 2.8" ILI9341 "2432S028R" variant is:
// TFT_MISO 12, TFT_MOSI 13, TFT_SCLK 14, TFT_CS 15, TFT_DC 2, TFT_RST -1, TFT_BL 21

TFT_eSPI tft = TFT_eSPI();         // Invoke custom library
int imageOffsetX = 26, imageOffsetY = 20;
// TFT_eSprite spr = TFT_eSprite(&tft);  // Declare Sprite object "spr" with pointer to "tft" object


// Screen layout - computed once in setup() from the actual panel size (not hardcoded)
// isn't hardcoded to one screen resolution/rotation.
int albumX, albumY, albumSize;   // album art box 
int textX, textW;                // artist/song text column,
int barX, barY, barWidth, barHeight = 10; // progress bar

// // This next function will be called during decoding of the jpeg file to
// // render each block to the TFT.  If you use a different TFT library
// // you will need to adapt this function to suit.
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  // Stop further decoding as image is running off bottom of screen
  if ( y >= tft.height() ) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);
 
  // Return 1 to decode next block
  return 1;
}

// Album Art 2-color gradient
void drawGradientBackground(uint16_t colorTop, uint16_t colorBottom){ 
    uint8_t r1 = (colorTop >> 11) & 0x1F, g1 = (colorTop >> 5) & 0x3F, b1 = colorTop & 0x1F; 
    uint8_t r2 = (colorBottom >> 11) & 0x1F, g2 = (colorBottom >> 5) & 0x3F, b2 = colorBottom & 0x1F; 
    int h = tft.height(); 
    for (int y = 0; y < h; y++) { 
        float t = (float)y / (float)(h - 1); 
        uint8_t r = r1 + (uint8_t)((int)(r2 - r1) * t);
        uint8_t g = g1 + (uint8_t)((int)(g2 - g1) * t);
        uint8_t b = b1 + (uint8_t)((int)(b2 - b1) * t); 
    uint16_t color = (r << 11) | (g << 5) | b;
        tft.drawFastHLine(0, y, tft.width(), color);
    }
} 

//samples a fixed number of pixels via reservoir sampling 
// then runs a small 2-means clustering pass on them to find two 
// actually distinct dominant colors for the gradient.
#define COLOR_SAMPLE_MAX 64 
#define COLOR_SAMPLE_MAX 64
uint8_t sampleR[COLOR_SAMPLE_MAX], sampleG[COLOR_SAMPLE_MAX], sampleB[COLOR_SAMPLE_MAX];
uint32_t colorSampleCount = 0; // total pixels seen (for reservoir sampling), not the array length
bool color_sample_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)		
{
    uint32_t n = (uint32_t)w * h;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t p = bitmap[i];
        uint8_t r8 = ((p >> 11) & 0x1F) << 3; // 5-bit -> 8-bit
        uint8_t g8 = ((p >> 5) & 0x3F) << 2;  // 6-bit -> 8-bit
        uint8_t b8 = (p & 0x1F) << 3;         // 5-bit -> 8-bit
        if (colorSampleCount < COLOR_SAMPLE_MAX) {
            sampleR[colorSampleCount] = r8;
            sampleG[colorSampleCount] = g8;
            sampleB[colorSampleCount] = b8;
        } else {
            uint32_t j = random(0, colorSampleCount + 1);
            if (j < COLOR_SAMPLE_MAX) {
                sampleR[j] = r8; sampleG[j] = g8; sampleB[j] = b8;
            }
        }
        colorSampleCount++;
    }
    return 1; // keep decoding, we just don't draw these blocks
}

// Runs a few iterations of 2-means on the sampled pixels and packs the two
// resulting cluster centers into RGB565, lighter one first. sampleCount is
// how many of the COLOR_SAMPLE_MAX slots are actually filled (min of
// colorSampleCount and COLOR_SAMPLE_MAX).
void computeTwoDominantColors(int sampleCount, uint16_t &lighter, uint16_t &darker) {
    int idxMin = 0, idxMax = 0, minLum = 9999, maxLum = -1;
    for (int i = 0; i < sampleCount; i++) {
        int lum = sampleR[i] + sampleG[i] + sampleB[i];
        if (lum < minLum) { minLum = lum; idxMin = i; }
        if (lum > maxLum) { maxLum = lum; idxMax = i; }
    }
    float c0r = sampleR[idxMin], c0g = sampleG[idxMin], c0b = sampleB[idxMin];
    float c1r = sampleR[idxMax], c1g = sampleG[idxMax], c1b = sampleB[idxMax];

    for (int iter = 0; iter < 4; iter++) {
        float sum0r=0, sum0g=0, sum0b=0; int count0=0;
        float sum1r=0, sum1g=0, sum1b=0; int count1=0;
        for (int i = 0; i < sampleCount; i++) {
            float dr0 = sampleR[i]-c0r, dg0 = sampleG[i]-c0g, db0 = sampleB[i]-c0b;
            float dr1 = sampleR[i]-c1r, dg1 = sampleG[i]-c1g, db1 = sampleB[i]-c1b;
            if ((dr0*dr0+dg0*dg0+db0*db0) <= (dr1*dr1+dg1*dg1+db1*db1)) {
                sum0r+=sampleR[i]; sum0g+=sampleG[i]; sum0b+=sampleB[i]; count0++;
            } else {
                sum1r+=sampleR[i]; sum1g+=sampleG[i]; sum1b+=sampleB[i]; count1++;
            }
        }
        if (count0 > 0) { c0r=sum0r/count0; c0g=sum0g/count0; c0b=sum0b/count0; }
        if (count1 > 0) { c1r=sum1r/count1; c1g=sum1g/count1; c1b=sum1b/count1; }
    }

    // A few very monochrome covers can converge both clusters to nearly the
    // same color, which would make the gradient invisible. If they're too
    // close, nudge them apart so there's still a visible top-to-bottom shift.
    float lumDiff = (c1r+c1g+c1b) - (c0r+c0g+c0b);
    if (abs(lumDiff) < 40) {
        c0r = min(255.0f, c0r+25); c0g = min(255.0f, c0g+25); c0b = min(255.0f, c0b+25);
        c1r = max(0.0f, c1r-25);   c1g = max(0.0f, c1g-25);   c1b = max(0.0f, c1b-25);
    }

    uint16_t colorA = (((uint8_t)c0r & 0xF8) << 8) | (((uint8_t)c0g & 0xFC) << 3) | ((uint8_t)c0b >> 3);
    uint16_t colorB = (((uint8_t)c1r & 0xF8) << 8) | (((uint8_t)c1g & 0xFC) << 3) | ((uint8_t)c1b >> 3);
    bool aIsLighter = (c0r+c0g+c0b) >= (c1r+c1g+c1b);
    lighter = aIsLighter ? colorA : colorB;
    darker  = aIsLighter ? colorB : colorA;
}


/*=========================
|User modifiable variables|
=========================*/
// WiFi credentials MOVED TO SECRET
//#define WIFI_SSID ""
//#define PASSWORD ""

// Spotify API credentials
//#define CLIENT_ID ""
//#define CLIENT_SECRET ""
#define REDIRECT_URI "https://spotifyesp32.vercel.app/api/spotify/callback"

// ESP32-CYD pin assignments -----------------------------------------------
// Most GPIOs on a CYD board are already used by the display, touch controller,
// SD card slot, and onboard speaker/LDR. These are commonly-free pins on the
// spare header, but check your specific board's pinout/schematic and adjust
// as needed.
//this project doesn't use the SD card, so its SPI pins are free — GPIO 5 (CS), 18 (CLK), 19 (MISO), 23 (MOSI). That gives you enough spare pins without touching UART.
#define BTN_PLAY_PAUSE 22 //GPIO
#define BTN_LIKE       23 // SD card sniffer
#define BTN_SKIP_FWD   27 //GPIO
#define BTN_SKIP_BACK  19 // SD card sniffer
#define VOL_POT_PIN    35   // input-only ADC1 pin, good for an analog pot
// ---------------------------------------------------------------------------

/*=========================
|Non - modifiable variables|
==========================*/

String getValue(HTTPClient &http, String key) {
  bool found = false, look = false, seek = true;
  int ind = 0;
  String ret_str = "";

  int len = http.getSize();
  char char_buff[1];
  WiFiClient * stream = http.getStreamPtr();
  while (http.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    // Serial.print("Size: ");
    // Serial.println(size);
    if (size) {
      int c = stream->readBytes(char_buff, ((size > sizeof(char_buff)) ? sizeof(char_buff) : size));
      if (found) {
        if (seek && char_buff[0] != ':') {
          continue;
        } else if(char_buff[0] != '\n'){
            if(seek && char_buff[0] == ':'){
                seek = false;
                int c = stream->readBytes(char_buff, 1);
            }else{
                ret_str += char_buff[0];
            }
        }else{
            break;
        }
          
        // Serial.print("get: ");
        // Serial.println(get);
      }
      else if ((!look) && (char_buff[0] == key[0])) {
        look = true;
        ind = 1;
      } else if (look && (char_buff[0] == key[ind])) {
        ind ++;
        if (ind == key.length()) found = true;
      } else if (look && (char_buff[0] != key[ind])) {
        ind = 0;
        look = false;
      }
    }
  }
//   Serial.println(*(ret_str.end()));
//   Serial.println(*(ret_str.end()-1));
//   Serial.println(*(ret_str.end()-2));
  if(*(ret_str.end()-1) == ','){
    ret_str = ret_str.substring(0,ret_str.length()-1);
  }
  return ret_str;
}

//http response struct
struct httpResponse{
    int responseCode;
    String responseMessage;
};

struct songDetails{
    int durationMs;
    String album;
    String artist;
    String song;
    String Id;
    bool isLiked;
};

char *parts[10];

// formats milliseconds as M:SS (e.g. 3:07) for the track-length label above
// the progress bar.
String formatDuration(int ms) {
    int totalSeconds = ms / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return String(buf);
} 

void printSplitString(String text,int maxLineSize, int yPos, int columnX, int columnWidth)
{
    int currentWordStart = 0;
    int spacedCounter = 0;
    int spaceIndex = text.indexOf(" ");
    
    while(spaceIndex != -1){
        // Serial.println(ESP.getFreeHeap());
        char *part = parts[spacedCounter]; 
        sprintf(part,text.substring(currentWordStart,spaceIndex).c_str());
        // Serial.println(ESP.getFreeHeap());
        // parts[spacedCounter] = part;
        currentWordStart = spaceIndex;
        spacedCounter++;
        spaceIndex = text.indexOf(" ",spaceIndex+1);
    }
    // Serial.println(ESP.getFreeHeap());
    char *part = parts[spacedCounter]; 
    sprintf(part,text.substring(currentWordStart,text.length()).c_str());
    // Serial.println(ESP.getFreeHeap());
    currentWordStart = spaceIndex;
    size_t counter = 0;
    currentWordStart = 0;
    while(counter <= spacedCounter){
        char printable[maxLineSize];
        char* printablePointer = printable;
        // sprintf in word at counter always
        sprintf(printablePointer,parts[counter]);
        //get length of first word
        int currentLen = 0;
        while(parts[counter][currentLen] != '\0'){
            currentLen++;
            printablePointer++;
        }
        counter++;
        while(counter <= spacedCounter){
            int nextLen = 0;
            while(parts[counter][nextLen] != '\0'){
                nextLen++;
            }
            if(currentLen + nextLen > maxLineSize)
                break;
            sprintf(printablePointer, parts[counter]);
            currentLen += nextLen;
            printablePointer += nextLen;
            counter++;
        }
        String output = String(printable);
        if(output[0] == ' ')
            output = output.substring(1);
        // Serial.println(output);
        // Center each wrapped line within its column (columnX..columnX+columnWidth)
        // instead of the whole screen, so text sits in the right-hand column
        // next to the album art rather than spanning edge-to-edge.
        tft.setCursor((int)(columnX + columnWidth/2 - tft.textWidth(output) / 2),tft.getCursorY());
        tft.println(output);
        // free(printable);
    }
    // Serial.println(ESP.getFreeHeap());
}

//Create spotify connection class
class SpotConn {
public:
	SpotConn(){
        client = std::make_unique<WiFiClientSecure>();
        client->setInsecure();
    }

	bool getUserCode(String serverCode) {
        https.begin(*client,"https://accounts.spotify.com/api/token");
        String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));
        https.addHeader("Authorization",auth);
        https.addHeader("Content-Type","application/x-www-form-urlencoded");
        String requestBody = "grant_type=authorization_code&code="+serverCode+"&redirect_uri="+String(REDIRECT_URI);
        // Send the POST request to the Spotify API
        int httpResponseCode = https.POST(requestBody);
        // Check if the request was successful
        if (httpResponseCode == HTTP_CODE_OK) {
            String response = https.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);
            accessToken = String((const char*)doc["access_token"]);
            refreshToken = String((const char*)doc["refresh_token"]);
            tokenExpireTime = doc["expires_in"];
            tokenStartTime = millis();
            accessTokenSet = true;
            Serial.println(accessToken);
            Serial.println(refreshToken);
        }else{
            Serial.println(https.getString());
        }
        // Disconnect from the Spotify API
        https.end();
        return accessTokenSet;
    }
    bool refreshAuth(){
        https.begin(*client,"https://accounts.spotify.com/api/token");
        String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));
        https.addHeader("Authorization",auth);
        https.addHeader("Content-Type","application/x-www-form-urlencoded");
        String requestBody = "grant_type=refresh_token&refresh_token="+String(refreshToken);
        // Send the POST request to the Spotify API
        int httpResponseCode = https.POST(requestBody);
        accessTokenSet = false;
        // Check if the request was successful
        if (httpResponseCode == HTTP_CODE_OK) {
            String response = https.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);
            accessToken = String((const char*)doc["access_token"]);
            // refreshToken = doc["refresh_token"];
            tokenExpireTime = doc["expires_in"];
            tokenStartTime = millis();
            accessTokenSet = true;
            Serial.println(accessToken);
            Serial.println(refreshToken);
        }else{
            Serial.println(https.getString());
        }
        // Disconnect from the Spotify API
        https.end();
        return accessTokenSet;
    }
    bool getTrackInfo(){
        String url = "https://api.spotify.com/v1/me/player/currently-playing";
        https.useHTTP10(true);
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.GET();
        bool success = false;
        String songId = "";
        bool refresh = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
                        // 

            String currentSongProgress = getValue(https,"progress_ms");
            currentSongPositionMs = currentSongProgress.toFloat();
            String imageLink = "";
            while(imageLink.indexOf("image") == -1){
                String height = getValue(https,"height");
                // Serial.println(height);
                if(height.toInt() > 300){
                    imageLink = "";
                    continue;
                }
                imageLink = getValue(https, "url");
                
                // Serial.println(imageLink);
            }
            // Serial.println(imageLink);
            
            
            String albumName = getValue(https,"name");
            String artistName = getValue(https,"name");
            String songDuration = getValue(https,"duration_ms");
            currentSong.durationMs = songDuration.toInt();
            String songName = getValue(https,"name");
            songId = getValue(https,"uri");
            String isPlay = getValue(https, "is_playing");
            isPlaying = isPlay == "true";
            Serial.println(isPlay);
            // Serial.println(songId);
            songId = songId.substring(15,songId.length()-1);
            // Serial.println(songId);
            https.end();
            // listSPIFFS();
            Serial.printf("[track] songId=%s currentSong.Id=%s\n", songId.c_str(), currentSong.Id.c_str());
            if (songId != currentSong.Id){
                Serial.println("[track] new song detected, will refresh art + gradient");
                
                if(SPIFFS.exists("/albumArt.jpg") == true) {
                    SPIFFS.remove("/albumArt.jpg");
                }
                // Serial.println("trying to get album art");
                bool loaded_ok = getFile(imageLink.substring(1,imageLink.length()-1).c_str(), "/albumArt.jpg"); // Note name preceded with "/"
                Serial.println("Image load was: ");
                Serial.println(loaded_ok);
                refresh = true;
                //tft.fillScreen(TFT_BLACK); moved to gradient repaint
            }
            currentSong.album = albumName.substring(1,albumName.length()-1);
            currentSong.artist = artistName.substring(1,artistName.length()-1);
            currentSong.song = songName.substring(1,songName.length()-1);
            currentSong.Id = songId;
            currentSong.isLiked = findLikedStatus(songId);
            success = true;
        } else {
            Serial.print("Error getting track info: ");
            Serial.println(httpResponseCode);
            // String response = https.getString();
            // Serial.println(response);
            https.end();
        }
        
        
        // Disconnect from the Spotify API
        if(success){
            drawScreen(refresh);
            lastSongPositionMs = currentSongPositionMs;
        }
        return success;
    }
    bool findLikedStatus(String songId){
        String url = "https://api.spotify.com/v1/me/tracks/contains?ids="+songId;
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        https.addHeader("Content-Type","application/json");
        int httpResponseCode = https.GET();
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
            String response = https.getString();
            https.end();
            return(response == "[ true ]");
        } else {
            Serial.print("Error toggling liked songs: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
            https.end();
        }

        
        // Disconnect from the Spotify API
        
        return success;
    }
    bool toggleLiked(String songId){
        String url = "https://api.spotify.com/v1/me/tracks/contains?ids="+songId;
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        https.addHeader("Content-Type","application/json");
        int httpResponseCode = https.GET();
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
            String response = https.getString();
            https.end();
            if(response == "[ true ]"){
                currentSong.isLiked = false;
                removeLikedSong(songId);
            }else{
                currentSong.isLiked = true;
                likeSong(songId);
            }
            drawScreen(false,true);
            Serial.println(response);
            success = true;
        } else {
            Serial.print("Error toggling liked songs: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
            https.end();
        }

        
        // Disconnect from the Spotify API
        
        return success;
    }

    bool drawScreen(bool fullRefresh = false, bool likeRefresh = false, bool playRefresh = false){
       // int rectWidth = 120;
        //int rectHeight = 10;
        if(fullRefresh){
            // Sample the album art and derive the gradient from its two most
            // dominant colors (computeTwoDominantColors) rather than a
            // flat average, Falls back to a
            // neutral dark tone if there's no art yet.
            bgColorTop = 0x2104;    // neutral dark gray fallback (no art yet)
            bgColorBottom = 0x0000; // black
            if (SPIFFS.exists("/albumArt.jpg") == true) {
                colorSampleCount = 0;
                TJpgDec.setSwapBytes(true);
                TJpgDec.setJpgScale(4);
                TJpgDec.setCallback(color_sample_output);
                TJpgDec.drawFsJpg(0, 0, "/albumArt.jpg");
                TJpgDec.setCallback(tft_output); // restore normal drawing callback
                int usedSamples = min((uint32_t)COLOR_SAMPLE_MAX, colorSampleCount);
                Serial.printf("[gradient] sampled %u pixels (of %u seen) from albumArt.jpg\n", usedSamples, (unsigned)colorSampleCount);
                if (usedSamples > 0) {
                    computeTwoDominantColors(usedSamples, bgColorTop, bgColorBottom);
                } else {
                    Serial.println("[gradient] WARNING: 0 pixels sampled - decode likely failed, using fallback gray");
                }
            }
            drawGradientBackground(bgColorTop, bgColorBottom); 

            if (SPIFFS.exists("/albumArt.jpg") == true) { 
                TJpgDec.setSwapBytes(true);
                TJpgDec.setJpgScale(2);
                TJpgDec.drawFsJpg(albumX, albumY, "/albumArt.jpg");
            }else{
                TJpgDec.setSwapBytes(false);
                TJpgDec.setJpgScale(1);
                TJpgDec.drawFsJpg(albumX, albumY, "/Angry.jpg");
            }
            tft.setTextDatum(MC_DATUM);
            tft.setTextWrap(true);
            tft.setTextColor(TFT_WHITE);

            tft.setTextSize(1);
            tft.setCursor(textX,albumY + 15);
            printSplitString(currentSong.artist,14,albumY + 15,textX,textW);

            tft.setTextSize(2);
            tft.setCursor(textX,albumY + 70);
            printSplitString(currentSong.song,7,albumY + 70,textX,textW); 
            tft.setTextSize(1); // reset

            // Track length, shown just above the progress bar.
            String durationText = formatDuration(currentSong.durationMs);
            int durationTextX = barX + barWidth/2 - tft.textWidth(durationText)/2;
            tft.setCursor(durationTextX, barY - 14);
            tft.println(durationText);
            
            tft.drawRoundRect(
                barX,
                barY,
                barWidth,
                barHeight,
                4,
                TFT_DARKGREEN);
        }
        if(fullRefresh || likeRefresh){
            // draw heart from primitives
            int hx = tft.width()-21, hy = 0;
            tft.fillRect(hx, hy, 21, 21, bgColorTop); // clear the icon's box first
            if(currentSong.isLiked){
                tft.fillCircle(hx+6, hy+7, 5, TFT_RED);
                tft.fillCircle(hx+15, hy+7, 5, TFT_RED);
                tft.fillTriangle(hx+1, hy+8, hx+20, hy+8, hx+10, hy+20, TFT_RED);
            }
            // else: left cleared/blank
        }
        if(fullRefresh || playRefresh){
            // Play/pause drawn the same way, sitting just to the heart's left.
            int px = tft.width()-44, py = 0;
            tft.fillRect(px, py, 21, 21, bgColorTop); // clear the icon's box first
            if(isPlaying){
                tft.fillRect(px+5, py+3, 4, 15, TFT_WHITE);
                tft.fillRect(px+12, py+3, 4, 15, TFT_WHITE);
            }else{
                tft.fillTriangle(px+6, py+3, px+6, py+18, px+18, py+10, TFT_WHITE);
            }
        }
        if(lastSongPositionMs > currentSongPositionMs){
            tft.fillSmoothRoundRect(
                barX + 2,
                barY + 2,
                barWidth - 4,
                barHeight - 4,
                10,
                bgColorBottom
            );
            lastSongPositionMs = currentSongPositionMs;
        }
        tft.fillSmoothRoundRect(
                barX + 2,
                barY + 2,
                barWidth * (currentSongPositionMs/currentSong.durationMs) - 4,
                barHeight - 4,
                10,
                TFT_GREEN
        );
        // Serial.println(currentSongPositionMs);
        // Serial.println(currentSong.durationMs);
        // Serial.println(currentSongPositionMs/currentSong.durationMs);
        return true;
    }
    bool togglePlay(){
        String url = "https://api.spotify.com/v1/me/player/" + String(isPlaying ? "pause" : "play");
        isPlaying = !isPlaying;
        drawScreen(false, false, true); // redraw just the play/pause icon immediately 
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.PUT("");
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 204) {
            // String response = https.getString();
            Serial.println((isPlaying ? "Playing" : "Pausing"));
            success = true;
        } else {
            Serial.print("Error pausing or playing: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        getTrackInfo();
        return success;
    }
    bool adjustVolume(int vol){
        String url = "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(vol);
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.PUT("");
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 204) {
            // String response = https.getString();
            currVol = vol;
            success = true;
        }else if(httpResponseCode == 403){
             currVol = vol;
            success = false;
            Serial.print("Error setting volume: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        } else {
            Serial.print("Error setting volume: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        return success;
    }
    bool skipForward(){
        String url = "https://api.spotify.com/v1/me/player/next";
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.POST("");
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 204) {
            // String response = https.getString();
            Serial.println("skipping forward");
            success = true;
        } else {
            Serial.print("Error skipping forward: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        getTrackInfo();
        return success;
    }
    bool skipBack(){
        String url = "https://api.spotify.com/v1/me/player/previous";
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.POST("");
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 204) {
            // String response = https.getString();
            Serial.println("skipping backward");
            success = true;
        } else {
            Serial.print("Error skipping backward: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        getTrackInfo();
        return success;
    }
    bool likeSong(String songId){
        String url = "https://api.spotify.com/v1/me/tracks?ids="+songId;
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        https.addHeader("Content-Type","application/json");
        char requestBody[] = "{\"ids\":[\"string\"]}";
        int httpResponseCode = https.PUT(requestBody);
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
            // String response = https.getString();
            Serial.println("added track to liked songs");
            success = true;
        } else {
            Serial.print("Error adding to liked songs: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        return success;
    }
    bool removeLikedSong(String songId){
        String url = "https://api.spotify.com/v1/me/tracks?ids="+songId;
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        // https.addHeader("Content-Type","application/json");
        // char requestBody[] = "{\"ids\":[\"string\"]}";
        int httpResponseCode = https.sendRequest("DELETE");
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
            // String response = https.getString();
            Serial.println("removed liked songs");
            success = true;
        } else {
            Serial.print("Error removing from liked songs: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
        }

        
        // Disconnect from the Spotify API
        https.end();
        return success;
    }
    bool accessTokenSet = false;
    long tokenStartTime;
    int tokenExpireTime;
    songDetails currentSong;
    float currentSongPositionMs;
    float lastSongPositionMs;
    int currVol;
    // Current gradient background endpoints, sampled from the album art.
    // Kept here (not local to drawScreen) so the like-icon/progress-bar clear
    // fills on later partial-refresh calls can match the background that's
    // already on screen instead of using a stale/guessed color.
    uint16_t bgColorTop = TFT_BLACK;
    uint16_t bgColorBottom = TFT_BLACK; 
private:
    std::unique_ptr<WiFiClientSecure> client;
    HTTPClient https;
    bool isPlaying = false;
    String accessToken;
    String refreshToken;
};
//Vars for keys, play state, last song, etc.
bool buttonStates[] = {1,1,1,1};
int debounceDelay = 50;
unsigned long debounceTimes[] = {0,0,0,0};
int buttonPins[] = {BTN_PLAY_PAUSE, BTN_LIKE, BTN_SKIP_FWD, BTN_SKIP_BACK};
//Func to establish connection
//Func to refresh connection 
//Funcs for all api calls

//Create screen control class
//Show a face
//Show currently playing
//Show volume change

//Object instances
WebServer server(80); //Server on port 80
SpotConn spotifyConnection;

//Web server callbacks
void handleRoot() {
    Serial.println("handling root");
    char page[900];
    snprintf(page,sizeof(page),mainPage, CLIENT_ID, REDIRECT_URI);
    server.send(200, "text/html", String(page)+"\r\n"); //Send web page
}

void handleCallbackPage() {
    if(!spotifyConnection.accessTokenSet){
        if (server.arg("code") == ""){     //Parameter not found
            char page[900];
            snprintf(page,sizeof(page), errorPage,CLIENT_ID,REDIRECT_URI);
            server.send(200, "text/html", String(page)); //Send web page
        }else{     //Parameter found
            if(spotifyConnection.getUserCode(server.arg("code"))){
                server.send(200,"text/html","Spotify setup complete Auth refresh in :"+String(spotifyConnection.tokenExpireTime));
            }else{
                char page[500];
                sprintf(page,errorPage,CLIENT_ID,REDIRECT_URI);
                server.send(200, "text/html", String(page)); //Send web page
            }
        }
    }else{
        server.send(200,"text/html","Spotify setup complete");
    }
}
long timeLoop;
long refreshLoop;
bool serverOn = true;
/*==============
|Setup function|
==============*/
void setup(){
    Serial.begin(115200);
    // Seeds the reservoir sampling used by the gradient-color extraction 
    // (random() with no seed can repeat the same sequence across boots). 
    randomSeed(esp_random());
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialisation failed!!");
        while (1) yield(); 
    }
    Serial.println("\r\nInitialisation done.");

    // Initialise the TFT
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(1);
    tft.invertDisplay(true); //some esp-32 ship with photo-negative
    // The jpeg image can be scaled DOWN by a factor of 1, 2, 4, or 8
    TJpgDec.setJpgScale(2); //2 should be about 150x150
    // Compute layout now that the panel's real width/height are known (after
    // begin()+setRotation()). Album art sits top-left and large; artist/song
    // text sits in a column to its right; the progress bar runs along the
    // bottom of the screen.
    albumSize = min(tft.width() - 20, 160);   // cap so it doesn't dominate tiny screens
    albumX = 10;
    albumY = 10;
    textX = albumX + albumSize + 10;
    textW = tft.width() - textX - 10;
    barHeight = 10;
    barWidth = tft.width() - 40;
    barX = 20;
    barY = tft.height() - barHeight - 20; // near the bottom, with a small margin 

    // The byte order can be swapped (set true for TFT_eSPI)
    TJpgDec.setSwapBytes(true);

    // The decoder must be given the exact name of the rendering function above
    TJpgDec.setCallback(tft_output);

    WiFi.begin(WIFI_SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi\n Ip is: ");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);      //Which routine to handle at root location
    server.on("/callback", handleCallbackPage);      //Which routine to handle at root location
    server.begin();                  //Start server
    Serial.println("HTTP server started");
    for(int i = 0 ; i < 4; i++){
        pinMode(buttonPins[i],INPUT_PULLUP);
    }
    for(int i = 0 ; i < 10; i++){
        parts[i] = (char*)malloc(sizeof(char) * 20);
    }

    tft.println(WiFi.localIP());
}
// * Sets up WiFi
// * Shows Ip on screen
// * Goes through spotify API handshake (SpotConn func)
// * Initializes screen
// * Checks to see if anything is currently playing (SpotCon func)
// * Shows cute face if needed

void loop(){
    if(spotifyConnection.accessTokenSet){
        if(serverOn){
            server.close();
            serverOn = false;
        }
        if((millis() - spotifyConnection.tokenStartTime)/1000 > spotifyConnection.tokenExpireTime){
            Serial.println("refreshing token");
            if(spotifyConnection.refreshAuth()){
                Serial.println("refreshed token");
            }
        }
        if((millis() - refreshLoop) > 5000){
            spotifyConnection.getTrackInfo();
            
            // spotifyConnection.drawScreen();
            refreshLoop = millis();
        }
        for(int i = 0; i < 4; i ++){
            int reading = digitalRead(buttonPins[i]);
            if( reading != buttonStates[i]){
                if(millis() - debounceTimes[i] > debounceDelay){
                    buttonStates[i] = reading;
                    if(reading == LOW){
                        switch (i)
                        {
                        case 0:
                            spotifyConnection.togglePlay();
                            break;
                        case 1:
                            spotifyConnection.toggleLiked(spotifyConnection.currentSong.Id);
                            break;
                        case 2:
                            spotifyConnection.skipForward();
                            break;
                        case 3:
                            spotifyConnection.skipBack();
                            break;
                        
                        default:
                            break;
                        }
                    }
                    debounceTimes[i] = millis();
                }
            }
        }
        
        // ESP32 ADC is 12-bit (0-4095) vs ESP8266's 10-bit (0-1023)
        int volRequest = map(analogRead(VOL_POT_PIN),0,4095,0,100);
        if(abs(volRequest - spotifyConnection.currVol) > 2){
            spotifyConnection.adjustVolume(volRequest);
        }
        timeLoop = millis();
    }else{
        server.handleClient();
    }

}
