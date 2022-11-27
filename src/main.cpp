/******************************************************************

                        WiFi LoLin32
                                                    қuran nov 2022
******************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include "wlan.h"

#define TRUE                             true
#define FALSE                            false
#define WAIT_500_MSEC                    5000 

//#define BATTERY_LEVEL                    A0      // GPIO 36
#define BATTERY_LEVEL                    A3      // GPIO 39
#define LED                              5

// #define NEW_SYSTEM 

#define MOTOR_L                          2       // GPIO 2
#define MOTOR_L_DIR                      15      // GPIO 15
#define MOTOR_R                          32      // GPIO 32
#define MOTOR_R_DIR                      33      // GPIO 33

#define REFV                             50.     // factor

AsyncWebServer server(80);

const char* input_parameter1 = "input_string";
const char* speedLeft = "speedLeft";

//  TIMER INTERRUPT:
hw_timer_t *timer = NULL;
volatile uint16_t tim;
volatile int oneSecFlag;
volatile int speedL, speedR;                     // between -255 up to +255
volatile int countL, countR;
void IRAM_ATTR myTimer(void);

// SPEEDOMETER:
const uint8_t impulsL = 14;
const uint8_t impulsR = 27;
void impuls_R_isr(void);
void impuls_L_isr(void);



// R"()"   Rawliteral - innerhalb dieses Strings werden die / nicht interpretiert. darum kein /" usw... 

const char index_html[] = R"(
<!DOCTYPE HTML><html><head>
    <title>LoLin32 WiFi System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        html {font-family: Verdana; display: inline-block; text-align: center;}
        h2 {font-size: 3.0rem; color: #00FF00;}
    </style>
    <script>
        function message_popup()
        {
            setTimeout(function() {document.location.reload(false); }, 500);
        }
    </script>
    </head><body>
    <h2>HTL St.P&ouml;lten :: EL</h2>
    <br>
    battery-levl: %input_adc% Volt
    <br>
    <form action="/get" target="hidden-form">
        Enter string: <input type="text" name="input_string">
        <input type="submit" value="Submit">
    </form><br>
    <form action="/get" target="hidden-form">
        speed Left (-255 up to +255) - current value %input_speedL%: <input type="number" name="speedLeft">
        <input type="submit" value="Submit" onclick="message_popup() ">
    </form>
    <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)";

void notFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "Not found");
}

String read_file(fs::FS &fs, const char * path)
{
    // Serial.printf("Reading file: %s\r\n", path);
    File file = fs.open(path, "r");
    if(!file || file.isDirectory())
    {
        Serial.println("Empty file/Failed to open file");
        return String();
    }
    // Serial.println("- read from file:");
    String fileContent;
    while(file.available())
    {
        fileContent+=String((char)file.read());
    }
    file.close();
    // Serial.println(fileContent);
    return fileContent;
}

void write_file(fs::FS &fs, const char * path, const char * message)
{
    // Serial.printf("Writing file: %s\r\n", path);
    File file = fs.open(path, "w");
    if(!file)
    {
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message))
    {
        // Serial.println("SUCCESS in writing file");
    } else {
        // Serial.println("FAILED to write file");
    }
    file.close();
}

String processor(const String& var)
{
    if(var == "input_string")
    {
       return read_file(SPIFFS, "/input_string.txt");
    }  else if(var == "input_speedL")
    {
       return read_file(SPIFFS, "/input_speedL.txt");
    }  else if(var == "input_adc")
    {
       return read_file(SPIFFS, "/input_adc.txt");
    }
    return String();
}


void setup() 
{
    // TIMER INTERRUPT:
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &myTimer, true);
    timerAlarmWrite(timer, 100, true);  // 0.1 msec
    timerAlarmEnable(timer);
    oneSecFlag = FALSE; 

    // hard ware configuration: 
    pinMode(BATTERY_LEVEL, INPUT);
    pinMode(LED , OUTPUT);
    pinMode(MOTOR_L, OUTPUT);
    pinMode(MOTOR_L_DIR, OUTPUT);
    pinMode(MOTOR_R, OUTPUT);
    pinMode(MOTOR_R_DIR, OUTPUT);
    speedL = -100;
    speedR = -100;
    countL = countR = 0;

    pinMode(impulsL, INPUT);
    pinMode(impulsR, INPUT);

    attachInterrupt(digitalPinToInterrupt(impulsR), impuls_R_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(impulsL), impuls_L_isr, FALLING);



    tim = WAIT_500_MSEC; while (tim);

    Serial.begin(115200);
    Serial.println("start!");

    if(!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) 
    {
        Serial.println("Connecting...");
        return;
    }
    Serial.println();
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());


    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send_P(200, "text/html", index_html, processor); } ); 

    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) 
    { 
        String input_message;  
        String input_parameter;

        if (request->hasParam(input_parameter1)) 
        {
            input_message = request->getParam(input_parameter1)->value();
            input_parameter = input_parameter1;
        }
        else if (request->hasParam(speedLeft)) 
        {
            input_message = request->getParam(speedLeft)->value();
            input_parameter = speedLeft;

            speedL = input_message.toInt();
            speedR = speedL;
            write_file(SPIFFS, "/input_speedL.txt", input_message.c_str());
        }
        else 
        {
            input_message = "No message sent";
            input_parameter = "none";
        }
    
        Serial.println(input_message);
        request->send(200, "text/html", 
        "neuen Wert eingelesen("+ input_parameter + ") with value: " + input_message + "<br><a href=\"/\">Return to Home Page</a>");
    });
    server.onNotFound(notFound);
    server.begin();
}

void loop() 
{
    static int x = FALSE; 
    float adc0;
    char text[20]; 

    int myInteger = read_file(SPIFFS, "/input_speedL.txt").toInt();
    //int myInteger = read_file(SPIFFS, "/input_speedL.txt").toInt();
    //Serial.print("integer entered: ");
    //Serial.println(myInteger);

    if (oneSecFlag == TRUE)
    {
        oneSecFlag = FALSE;

        x = (x == HIGH) ? LOW : HIGH;
        digitalWrite(LED, x);

        adc0 = analogRead(BATTERY_LEVEL) / REFV; 
        sprintf(text,"%.2f",adc0);
        //sprintf(text,"5.4");
        write_file(SPIFFS, "/input_adc.txt", text);

        Serial.print(adc0); 
        Serial.print(" ");
        Serial.print(speedL);
        Serial.print(" ");
        x = digitalRead(MOTOR_L_DIR);
        Serial.print(x);

        Serial.print(" L: ");
        x = countL;
        Serial.print(x);
        Serial.print(" R: ");
        x = countR;
        Serial.print(x);

        Serial.println();
    }
}

//****************************************************************
// ISR Speedometer:
//****************************************************************

void impuls_L_isr(void)
{
    countL++;
}

void impuls_R_isr(void)
{
    countR++;
}

//****************************************************************
//****************************************************************

void IRAM_ATTR myTimer(void)   // periodic timer interrupt, expires each 1 msec
{
    static int32_t tick = 0;
    int l, r;
    
    
    tick++;

    if (tim) tim--;

    if ((tick % 10000) == 0) 
    {
        oneSecFlag = TRUE; 
    }

    // LEFT: 
#ifdef NEW_SYSTEM
    if (speedL > 0) 
    { 
        digitalWrite(MOTOR_L_DIR, LOW ); 
        l = +speedL; 
        if ((tick & 0xff) < l) digitalWrite(MOTOR_L, HIGH); else digitalWrite(MOTOR_L, LOW);
    }
    else            
    { 
        digitalWrite(MOTOR_L, LOW ); 
        l = -speedL; 
        if ((tick & 0xff) < l) digitalWrite(MOTOR_L_DIR, HIGH); else digitalWrite(MOTOR_L_DIR, LOW);
    }
#else
    if (speedL > 0) { digitalWrite(MOTOR_L_DIR, LOW ); l = +speedL; }
    else            { digitalWrite(MOTOR_L_DIR, HIGH); l = -speedL; }

    if ((tick & 0xff) < l) digitalWrite(MOTOR_L, HIGH); else digitalWrite(MOTOR_L, LOW);
#endif

    // RIGHT: 
#ifdef NEW_SYSTEM
    if (speedR > 0) 
    { 
        digitalWrite(MOTOR_R, LOW ); 
        r = -speedR; 
        if ((tick & 0xff) < l) digitalWrite(MOTOR_R_DIR, HIGH); else digitalWrite(MOTOR_R_DIR, LOW);
    }
    else            
    { 
        digitalWrite(MOTOR_R_DIR, LOW ); 
        r = +speedR; 
        if ((tick & 0xff) < l) digitalWrite(MOTOR_R, HIGH); else digitalWrite(MOTOR_R, LOW);
    }
#else
    if (speedR > 0) { digitalWrite(MOTOR_R_DIR, HIGH); r = +speedR; }
    else            { digitalWrite(MOTOR_R_DIR, LOW ); r = -speedR; }

    if ((tick & 0xff) < r) digitalWrite(MOTOR_R, HIGH); else digitalWrite(MOTOR_R, LOW);
#endif


}


