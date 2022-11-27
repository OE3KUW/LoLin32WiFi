/******************************************************************
                        WiFi LoLin32
                                                    қuran nov 2022
******************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "wlan.h"

const char* input_parameter1 = "input_string";
const char* input_parameter2 = "input_integer";

//WiFiServer serverWiFi(80);             // Port 80 
//String ledStatus = "off";

AsyncWebServer server(80);


void setup() 
{
  // put your setup code here, to run once:
  
}

void loop() 
{
  // put your main code here, to run repeatedly:
}