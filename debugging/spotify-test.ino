/*
Spotify Web API Wrapper designed for ESP32
V3 for new Spotify API changes started April 9, 2025.
cred:
https://github.com/FinianLandes/SpotifyEsp32
*/

#include <Arduino.h>
#include <WiFi.h>
#include "SpotifyEsp32.h"

const char* SSID = "";
const char* PASSWORD = "";

const char* CLIENT_ID = "";
const char* CLIENT_SECRET = "";

const char* REFRESH_TOKEN = "";

// Create an instance of the Spotify class (optional: specify retry count)
//Spotify sp(CLIENT_ID, CLIENT_SECRET); // w/o refresh token
Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN); // with refresh token

void setup() {
 Serial.begin(115200);
 connect_to_wifi();

 sp.begin();
//  while (!sp.is_auth()) {
//      sp.handle_client(); // Required for receiving the authorization code
//  }

//  Serial.printf("Refresh token: %s\n", sp.get_user_tokens().refresh_token); //print refresh token to serial monitor (dont have to reauthenticate everytime)
}

void loop() {
    String track = sp.current_track_name();
    String artists = sp.current_artist_names();
    Serial.printf("Currently Playing: %s by %s\n", track.c_str(), artists.c_str());
    delay(1000);
}

void connect_to_wifi() {
 WiFi.begin(SSID, PASSWORD);
 Serial.print("Connecting to WiFi...");
 while (WiFi.status() != WL_CONNECTED) {
     delay(1000);
     Serial.print(".");
 }
 Serial.println("\nConnected to WiFi!");
}
