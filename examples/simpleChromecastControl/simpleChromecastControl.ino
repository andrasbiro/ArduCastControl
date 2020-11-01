/**
 * This implements a simple chromecast control example, tested on wemos D1 mini,
 * but should work on any ESP8266 (and probably ESP32)
 * Current status gathered from chromecast will be printed on serial.
 * Buttons on D5/D6/D7 will act as pause/prev/next respectively
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "ArduCastControl.h"

#define B_SELECT D5
#define B_LEFT D6
#define B_RIGHT D7
#define CHROMECASTIP "192.168.1.12"


ArduCastControl cc = ArduCastControl();
bool bSelectPressed = false, bLeftPressed=false, bRightPressed=false;

void setup() {
 
  Serial.begin(115200);
  Serial.println("booted");

  ArduinoOTA.setHostname ("chromecastremote");
  ArduinoOTA.begin();

  pinMode(D8, OUTPUT); //common pin for keys, used for pulldown - should have a pulldown anyway
  pinMode(B_SELECT, INPUT_PULLUP);
  pinMode(B_LEFT, INPUT_PULLUP);
  pinMode(B_RIGHT, INPUT_PULLUP);
}

uint32_t lastUpdated = 0;
uint32_t updatePeriod = 5000;

uint32_t bLastUpdated = 0;
uint32_t bUpdatePeriod = 25;

void loop() {
  ArduinoOTA.handle();
  //wait for 5s to boot - this is useful in case of a bootloop to keep OTA running
  if ( millis() < 10000 )
    return;
    

  if ( millis() - lastUpdated > updatePeriod ) {
    if ( cc.getConnection() != WAIT_FOR_RESPONSE ){
      cc.dumpStatus();
    }
    int st;

    if ( cc.getConnection() == DISCONNECTED ){
      Serial.print("Connecting...");
      st = cc.connect(CHROMECASTIP);
      Serial.println(st);
    } else {
      //at this point, cc.volume and cc.isMuted should be valid 
      connection_t c = cc.loop();
      if ( c == WAIT_FOR_RESPONSE || c == CONNECT_TO_APPLICATION ){
        updatePeriod = 50; 
      } else if ( c == APPLICATION_RUNNING ){
        updatePeriod = 500;
        //at this point, all public fields describing the casting
        //(e.g. cc.artist, cc.title) should be valid
      } else {
        updatePeriod = 5000;
      }
    }
    lastUpdated = millis();
  }
  if ( millis() - bLastUpdated > bUpdatePeriod && cc.getConnection() == APPLICATION_RUNNING ){
    bool prevSelect = bSelectPressed;
    bool prevLeft = bLeftPressed;
    bool prevRight = bRightPressed;
    bSelectPressed = digitalRead(B_SELECT) == LOW;
    bRightPressed = digitalRead(B_RIGHT) == LOW;
    bLeftPressed = digitalRead(B_LEFT) == LOW;

    if ( !bSelectPressed && prevSelect ){ //select released
      cc.pause(true);
    }

    if ( !bLeftPressed && prevLeft ){ //left released
      cc.prev();
    }

    if ( !bRightPressed && prevRight ){ //right released
      cc.next();
    }

    bLastUpdated = millis();
  }
}
