/******************************************************************

                        LoLin32 WiFi
                                                    қuran dez 2022
******************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include "FastLED.h"  // wird im .pio/lipdeps/lolin32 gefunden! 
#include "wlan.h"

#define TRUE                             true
#define FALSE                            false
#define WAIT_500_MSEC                    5000 
#define WAIT_ONE_SEC                     10000
#define BATTERY_LEVEL                    A3      // GPIO 39
#define LED                              5

#define NUM_LEDS                         4
#define DATA_PIN                         23
#define CLOCK_PIN                        18

// #define NEW_SYSTEM 

#define MOTOR_L                          2       // GPIO 2
#define MOTOR_L_DIR                      15      // GPIO 15
#define MOTOR_R                          32      // GPIO 32
#define MOTOR_R_DIR                      33      // GPIO 33

#define REFV                             68.50     // factor
#define LEN                              200
#define N                                10

AsyncWebServer server(80);
AsyncEventSource events("/events");


const char* inputText = "inputText";
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

// FastLED:

CRGB leds[NUM_LEDS];



// R"()"   Rawliteral - innerhalb dieses Strings werden die / nicht interpretiert. darum kein /" usw... 


const char index_html[] = R"(
<!DOCTYPE HTML><html><head>
    <title>LoLin32 WiFi System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        html {font-family: Verdana; display: inline-block; text-align: center;}
        h2 {font-size: 2.0rem; color: #00cc00;}
    </style>
    <script>
        if (!!window.EventSource) 
        {
            var source = new EventSource('/events');
            source.addEventListener('open', function(e) { console.log("Events Connected");   }, false);
            source.addEventListener('error', function(e) {
                if (e.target.readyState != EventSource.OPEN) { console.log("Events Disconnected"); }
                                                                                             }, false);
            source.addEventListener('message', function(e) { console.log("message", e.data); }, false);

            source.addEventListener('adc', function(e) { console.log("adc", e.data);
                document.getElementById("adc").innerHTML = e.data;                          }, false);
        }
    </script>
    </head>
    <body>
    <h2>HTL St.P&ouml;lten :: EL</h2>
    <br>
    battery-levl: <span id="adc">%adc%</span> Volt
    <br>
    <br>
    <form action="/get" target="hidden-form">
        Enter string: <input type="text" name="inputText">
        <input type="submit" value="Submit">
    </form><br>
    <form action="/get" target="hidden-form">
        speed Left (-255 up to +255) - current value %input_speedL%: <input type="number" name="speedLeft">
        <input type="submit" value="Submit" onclick="message_popup() ">
    </form>
    <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)";

// handle fileSystem:

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
    if     (var == "inputText")    { return read_file(SPIFFS, "/input_Text.txt");    }  
    else if(var == "input_speedL") { return read_file(SPIFFS, "/input_speedL.txt");  }  
    return String();
}

void setup() 
{
    // hard ware configuration: 
    pinMode(BATTERY_LEVEL, INPUT);
    pinMode(LED , OUTPUT);
    pinMode(MOTOR_L, OUTPUT);
    pinMode(MOTOR_L_DIR, OUTPUT);
    pinMode(MOTOR_R, OUTPUT);
    pinMode(MOTOR_R_DIR, OUTPUT);
    speedL = 0; // -255 up to +255
    speedR = 0; // -255 up to +255
    countL = countR = 0;

    pinMode(impulsL, INPUT);
    pinMode(impulsR, INPUT);

//  FastLED:

    FastLED.addLeds<SK9822, DATA_PIN, CLOCK_PIN, RBG>(leds, NUM_LEDS);

    leds[0] = CRGB{255,   0,   0};  
    leds[1] = CRGB{  0, 255,   0};
    leds[2] = CRGB{  0,   0, 255};
    leds[3] = CRGB{255, 255, 255};

    FastLED.show();


    attachInterrupt(digitalPinToInterrupt(impulsR), impuls_R_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(impulsL), impuls_L_isr, FALLING);

    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &myTimer, true);
    timerAlarmWrite(timer, 100, true);  // 0.1 msec
    timerAlarmEnable(timer);
    oneSecFlag = FALSE; 

    tim = WAIT_ONE_SEC; while (tim);

    Serial.begin(115200);
    Serial.println("start!");

    if(!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

     tim = WAIT_ONE_SEC; while (tim);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) 
    {
        Serial.println("Connecting...");
        return;
    }

    Serial.println();
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/",    HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html, processor); } );  
    
    server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){ 
        String input_message;  
        String input_parameter;

        if (request->hasParam(inputText)) 
        {
            input_message = request->getParam(inputText)->value();
            input_parameter = inputText;
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

    
    // Handle Web Server Events
    events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
                       }
    //-- send event with message "hello!", id current millis
    //-- and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
                       });
    server.addHandler(&events);
    
    server.begin();

}

void loop() 
{
    static int x = FALSE; 
    static int heart = TRUE;
    static int i = 0;
    static int j = 0;
    static float adcArray[N];
    static float oldAdc;
    static float newAdc;
    static int secCounter = 0;
    float sum;
    
    char text[LEN]; 

    int myInteger = read_file(SPIFFS, "/input_speedL.txt").toInt();

    if (oneSecFlag == TRUE)
    {
        oneSecFlag = FALSE;
        secCounter++;

        heart = (heart == HIGH) ? LOW : HIGH;
        digitalWrite(LED, heart);

        // average:
        adcArray[i] = analogRead(BATTERY_LEVEL)/ REFV;
        i++;  
        if (i == N) i = 0;
        sum = 0;  
        for (j = 0; j < N; j++) sum += adcArray[j];
        newAdc = sum / (float)N;

        if (((newAdc - oldAdc) > 0.02) || (secCounter > 4))
        {
            sprintf(text, "send new event! %.2f Volt", newAdc);  
            Serial.println(text);
            oldAdc = newAdc;
            secCounter = 0;
           
            events.send(String(newAdc).c_str(),"adc",millis());
        } 
        
    }
}

//** ISR Speedometer: ********************************************

void impuls_L_isr(void)
{
    countL++;
}

void impuls_R_isr(void)
{
    countR++;
}

//** Timer Interrupt:   ******************************************

void IRAM_ATTR myTimer(void)   // periodic timer interrupt, expires each 0.1 msec
{
    static int32_t tick = 0, tickR = 0;
    int l, r;
    
    
    tick++;
    tickR++;

    if (tim) tim--;

    if (tick == 10000) 
    {
        oneSecFlag = TRUE;
        tick = 0; 
    }

    // LEFT: 
#ifdef NEW_SYSTEM
    if (speedL > 0) 
    { 
        digitalWrite(MOTOR_L_DIR, LOW ); 
        l = +speedL; 
        if ((tickR & 0xff) < l) digitalWrite(MOTOR_L, HIGH); else digitalWrite(MOTOR_L, LOW);
    }
    else            
    { 
        digitalWrite(MOTOR_L, LOW ); 
        l = -speedL; 
        if ((tickR & 0xff) < l) digitalWrite(MOTOR_L_DIR, HIGH); else digitalWrite(MOTOR_L_DIR, LOW);
    }
#else
    if (speedL > 0) { digitalWrite(MOTOR_L_DIR, LOW ); l = +speedL; }
    else            { digitalWrite(MOTOR_L_DIR, HIGH); l = -speedL; }

    if ((tickR & 0xff) < l) digitalWrite(MOTOR_L, HIGH); else digitalWrite(MOTOR_L, LOW);
#endif

    // RIGHT: 
#ifdef NEW_SYSTEM
    if (speedR > 0) 
    { 
        digitalWrite(MOTOR_R, LOW ); 
        r = -speedR; 
        if ((tickR & 0xff) < l) digitalWrite(MOTOR_R_DIR, HIGH); else digitalWrite(MOTOR_R_DIR, LOW);
    }
    else            
    { 
        digitalWrite(MOTOR_R_DIR, LOW ); 
        r = +speedR; 
        if ((tickR & 0xff) < l) digitalWrite(MOTOR_R, HIGH); else digitalWrite(MOTOR_R, LOW);
    }
#else
    if (speedR > 0) { digitalWrite(MOTOR_R_DIR, HIGH); r = +speedR; }
    else            { digitalWrite(MOTOR_R_DIR, LOW ); r = -speedR; }

    if ((tickR & 0xff) < r) digitalWrite(MOTOR_R, HIGH); else digitalWrite(MOTOR_R, LOW);
#endif


}


