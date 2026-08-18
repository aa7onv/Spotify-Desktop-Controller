/***************************************************************************************
** Function name:           listSPIFFS
** Description:             Listing SPIFFS files
***************************************************************************************/
void listSPIFFS(void) {
  Serial.println(F("\r\nListing SPIFFS files:"));

  File root = SPIFFS.open("/"); // Root directory
  if (!root || !root.isDirectory()) {
    Serial.println(F("Failed to open SPIFFS root directory"));
    return;
  }

  static const char line[] PROGMEM =  "=================================================";
  Serial.println(FPSTR(line));
  Serial.println(F("  File name                              Size"));
  Serial.println(FPSTR(line));

  File f = root.openNextFile();
  while (f) {
    String fileName = String(f.name());
    Serial.print(fileName);
    int spaces = 33 - fileName.length(); // Tabulate nicely
    if (spaces < 1) spaces = 1;
    while (spaces--) Serial.print(" ");

    String fileSize = (String) f.size();
    spaces = 10 - fileSize.length(); // Tabulate nicely
    if (spaces < 1) spaces = 1;
    while (spaces--) Serial.print(" ");
    Serial.println(fileSize + " bytes");

    f = root.openNextFile();
  }

  Serial.println(FPSTR(line));
  Serial.println();
  delay(1000);
}
