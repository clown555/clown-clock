//https://randomnerdtutorials.com/esp8266-nodemcu-date-time-ntp-client-server-arduino/

#include <ESP8266WiFi.h>
//#include <Arduino.h>

// NTP
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// WIFI MANAGER
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// TM1637 - DISPLAY
#include <TM1637TinyDisplay.h>
#define CLK D1
#define DIO D2
TM1637TinyDisplay display(CLK, DIO);

// RTC
#include <DS3231.h>
#include <Wire.h> 
DS3231 rtc;

bool wifiPresent, rtcPresent;

byte lastDisplayedMinute;
byte currentHour;
byte currentMinute;
byte lastNTPHour = -1;

uint32_t lastRTC = 0; 
uint32_t rtcInterval = 5000; // read rtc every 5s

// ---------------------------------------------------------------------  SETUP
void setup() {
  Serial.begin(9600);
  for(int i=0;i<30;i++) Serial.print(i);
  Serial.println("-------------------------");
  Serial.println("CLOWN'S CLOCK");
  
  display.setBrightness(BRIGHT_1);
  display.showLevel(50,true);

  Wire.begin(D5, D6);
  Wire.setClock(400000L);   // set I2C clock to 400kHz

  rtc.setClockMode(false); // 24 hours mode
  
  Serial.println("Setting up WIFI");
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  //wm.resetSettings(); //for testing purposes - resets wifimanager => forget all wifi config, start AP 
  wm.setConfigPortalTimeout(180);
  
  if(!wm.autoConnect("CLOWN_CLOCK")){
    wifiPresent = false;
    Serial.println("Connect to any WIFI failed");  
  } else {
    Serial.println("WIFI connected");
    wifiPresent = true;
    timeClient.begin();
    timeClient.setTimeOffset(3600); // GMT offset in seconds
    getNTPTime();
  }  
}

/**
 * ptm structure 
 * tm_sec int seconds after the minute  0-61*
 * tm_min  int minutes after the hour  0-59
 * tm_hour int hours since midnight  0-23
 * tm_mday int day of the month  1-31
 * tm_mon  int months since January  0-11
 * tm_year int years since 1900  
 * tm_wday int days since Sunday 0-6
 * tm_yday int days since January 1  0-365
 * tm_isdst  int Daylight Saving Time flag // most probably US DST :(((
 * http://www.cplusplus.com/reference/ctime/tm/
*/

// --------------------------------------------------------------------- GET NTP 
void getNTPTime(){
  lastNTPHour = currentHour;
  timeClient.update();   
  uint32_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  currentHour = ptm -> tm_hour + ptm -> tm_isdst * 3600 ;
  currentMinute = ptm -> tm_min;
  
  char s[50];
  sprintf(s, "[NTP] TIME: %2d:%2d:%2d", ptm -> tm_hour, ptm -> tm_min, ptm -> tm_sec);
  Serial.println(s);
  if(currentHour ==7 && currentMinute == 28){
     // just trying to catch non-sense time response from NTP
    Serial.println("----------------------------------- 7:28");
    sprintf(s, "DATE: %2d.%2d.%2d WDAY:%2d YDAY:%3d DST:%2d", ptm -> tm_mday, ptm -> tm_mon + 1, ptm -> tm_year, ptm -> tm_wday, ptm -> tm_yday, ptm -> tm_isdst);
    Serial.println(s);
    Serial.println("----------------------------------- 7:28");    
  }
  rtc.setHour(currentHour); 
  rtc.setMinute(currentMinute); 
}

bool isHourInvalid(){
  return ((currentHour<0) || (currentHour>24));
}
// --------------------------------------------------------------------- GET RTC 
void getRTCTime(){
  bool rtcH12 = false;
  bool rtcPM = false;
  currentHour = rtc.getHour(rtcH12,rtcPM);
  currentMinute = rtc.getMinute(); 

  byte tries = 0;
  while(isHourInvalid() && tries < 10){
    currentHour = rtc.getHour(rtcH12,rtcPM);
    currentMinute = rtc.getMinute();
    tries ++;
    delay(500);
  }

  if(isHourInvalid()){
    Serial.print("non sense hour:rtcPresent goes false [");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.print(currentMinute);
    Serial.print("] ");
    rtcPresent = false;
    if(wifiPresent){
      getNTPTime();
    } else {
      Serial.println("NO WIFI + NO RTC");
      ESP.reset();  
    }
    return;
  } else {
    rtcPresent = true;  
  }

  // sync time with NTP every hour
  if(currentHour != lastNTPHour) {
    getNTPTime();
  }
}

bool isTimeToReadRTC(){
  uint32_t currentMillis = millis();
  uint32_t tmp = (currentMillis > lastRTC) ? (currentMillis - lastRTC) : (lastRTC - currentMillis);
  return tmp > rtcInterval;
}

// --------------------------------------------------------------------- READ SERIAL
void readSerial(){
  char myCmd[15] = "";
  char oneChar;
  if (Serial.available() > 0) {
    delay(100);    
    while (Serial.available() > 0) {
      oneChar = Serial.read();
      if((int)(oneChar)>32) sprintf(myCmd, "%s%c", myCmd, oneChar);   
    }
    processCMD(myCmd);
    sprintf(myCmd,"");
  }
}

// ---------------------------------------------------------------------  PROCESS CMD
void processCMD(char *cmd) {
  int val;
  int r;
  int xhour, xminute, xsecond, xvalue;
  Serial.print("PROCESSING COMMAND:");
  Serial.print(cmd);
  Serial.print(" ");
  r = sscanf(cmd, "T%02d%02d%02d", &xhour, &xminute, &xsecond);
  if (r == 3) {    
    char cas[22];
    sprintf(cas, "SET TIME: %02d:%02d:%02d", xhour, xminute, xsecond);
    Serial.println(cas);
    rtc.setHour(xhour);
    rtc.setMinute(xminute);
    rtc.setSecond(xsecond);
    getRTCTime();    
    return;
  }
  r = sscanf(cmd, "B%d", &xvalue);
  Serial.print(r);
  Serial.print(" ");
  if (r == 3) {    
    if(xvalue>0 && xvalue<8){
      display.setBrightness(xvalue);  
      Serial.print("BRIGHTNESS SET TO:");
      Serial.println(xvalue);
      return;
    } 
    Serial.println("Wrong BRIGHTNESS value. Please pick [1-7]"); 
    return;   
  }  
  if(strcmp(cmd, "HELP") == 0){
    Serial.println("NAPOVEDA K PRIKAZUM");  
    Serial.println("---------------------------");  
    Serial.println("THHMMSS - nastavi cas [T152030]");
    Serial.println("BX - nastavi jas displaye 1-7 [B1]"); 
    Serial.println("RESET - resetuje hodiny");  
    Serial.println("HELP - zobrazi tuto napovedu"); 
    return; 
  }
  if(strcmp(cmd, "RESET") == 0){
    Serial.println("RESET IN 5sec.");  
    delay(5000); 
    ESP.reset();
    return;
  }
}


// ---------------------------------------------------------------------  
void loop() {
  readSerial();
  
  if(isTimeToReadRTC()){
    getRTCTime();
  }
  
  if(currentMinute != lastDisplayedMinute){
    lastDisplayedMinute = currentMinute;
    display.showNumber(currentMinute/100.0 + currentHour,2);
    char string[10];
    sprintf(string, "[LOOP] EVERY MINUTE: %2d:%2d", currentHour, currentMinute);
    Serial.println(string);   
  } 
}
