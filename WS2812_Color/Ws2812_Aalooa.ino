//==================================================================================================
//                LED WS2812
//==================================================================================================
#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <EEPROM.h>  //For EEEPROM library inclusion
#include <ESP8266WiFi.h> //For ESP wifi library
#include <WiFiUdp.h>      // To enable UDP over wifi
#include <StringTokenizer.h>
#include <WebSocketsServer.h>
//#include <ESP8266mDNS.h>
#include <Hash.h>
#include <WS2812FX.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <string.h>
#include <NTPClient.h>

extern "C"{
#include "spi_flash.h"
#include "ets_sys.h"
}

#define DEBUG 0
#define EEPROM_SIZE 512
#define FACTORY_RESET_PIN 2
#define UDP_PORT  6755
#define TCP_PORT  9999

#define LED_COUNT 10
#define LED_PIN D2

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);; 

char ap_ssid[30]={'C','o','n','n','e','c','t','e','d','A','P','-'};
char *ap_password = "123456789";
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
WiFiUDP udp;
IPAddress    apIP(192, 168, 7, 1);
IPAddress ipMulti(239, 255, 255, 255);

#define NTP_OFFSET   60 * 60      // In seconds
#define NTP_INTERVAL 60 * 1000    // In miliseconds
#define NTP_ADDRESS  "in.pool.ntp.org"
Ticker flipper;

enum Eeprom_Data_address{
  INITIALIZED = 0,
  LED_CNT = 1,
  COLOR = 2,//PWM active or not
  COLOR_ONOFF = 3,
  COLOR_NAME = 4, //Name of the device in room
  COLOR_INTENSITY = 5, // 255x 1/2/3/4
  COLOR_R = 6, //DutyCycle
  COLOR_G = 7,
  COLOR_B = 8, 
  RELAY1 = 9, //add 7 to accomodate following data Active : on/off : type : intensity : R : G : B
  RELAY2 = 16,
  RELAY3 = 23,
  RELAY4 = 30,
  RELAY5 = 37,
  RELAY6 = 44,
  RELAY7 = 51,
  RELAY8 = 58
};


String TxRx_string,temp;
String Parsed_data[5]; //Max number of commad data is 5
String mode_ssid_pswd[4];
String WS2812_CODE[9];
String WS2812_LED_CNT[2];
String WS2812_LED_SPD[2];
String WS2812_LED_BRT[2];
String DEVICE_INIT[2];

// List of all color modes
enum MODE { OFF, ON };

MODE mode = OFF;        // Standard mode that is active when software starts

int ws2812fx_speed = 196;   // Global variable for storing the delay between color changes --> smaller == faster
int brightness = 196;       // Global variable for storing the brightness (255 == 100%)

int ws2812fx_mode = 0;      // Helper variable to set WS2812FX modes
int led_count = 20;
struct ledstate             // Data structure to store a state of a single led
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

typedef struct ledstate LEDState;     // Define the datatype LEDState
LEDState main_color = { 255, 0, 0 };  // Store the "main color" of the strip used in single color modes

char current_state[32];               // Keeps the current state representation
char last_state[32];                  // Save the last state as string representation
unsigned long time_statechange = 0;   // Time when the state last changed
int timeout_statechange_save = 5000;  // Timeout in ms to wait before state is saved
bool state_save_requested = false;    // State has to be saved after timeout

#define BTN_MODE_SHORT "WS2| 1|  0|245|196|255|255|255"   // Static white
#define BTN_MODE_MEDIUM "WS2| 1| 48|245|196|255|102|  0"    // Fire flicker
#define BTN_MODE_LONG "WS2| 1| 46|253|196|255|102|  0"  // Fireworks random

unsigned long keyPrevMillis = 0;
const unsigned long keySampleIntervalMs = 25;
byte longKeyPressCountMax = 80;       // 80 * 25 = 2000 ms
byte mediumKeyPressCountMin = 20;     // 20 * 25 = 500 ms
byte KeyPressCount = 0;
byte prevKeyState = HIGH;             // button is active low
boolean buttonState = false;


WebSocketsServer webSocket = WebSocketsServer(TCP_PORT);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

#define CONFIG_WIFI_SECTOR 0x7E
void config_init_default()
{
    ETS_UART_INTR_DISABLE();
    spi_flash_erase_sector(CONFIG_WIFI_SECTOR);
    ETS_UART_INTR_ENABLE();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
#if DEBUG        
            Serial.printf("[%u] Disconnected!\n", num);
#endif            
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
#if DEBUG
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            // send message to client
            //webSocket.sendTXT(num, "Connected");
           
            webSocket.broadcastTXT("Connected New Client"); 
#endif            
        }
            break;
        case WStype_TEXT:
#if DEBUG
            Serial.printf("[%u] get Text: %s\n", num, payload);
            //webSocket.sendTXT(num, payload);
#endif  
            //webSocket.broadcastTXT("OK");
            TxRx_string = String((char *)payload);
            //parse command based on data received.
            Command_Parsing();
            // send data to all connected clients 
            //webSocket.broadcastTXT(TxRx_string);
            TxRx_string = ""; 
            break;

    }

}

void flip()
{
  String formattedTime = timeClient.getFormattedTime();
  temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=" + formattedTime;
  webSocket.broadcastTXT(temp);
  temp = "";
  formattedTime = "";
}


// =================================================================================================
// Function : WiFiEvent
// Description: This function detects all the WiFi Events
// =================================================================================================
void WiFiEvent(WiFiEvent_t event) {
#if DEBUG
    Serial.printf("[WiFi-event] event: %d\n", event);
#endif
    switch(event) {
        case WIFI_EVENT_STAMODE_CONNECTED:
#if DEBUG
            Serial.println("WiFi station connected");
#endif
            break;
        case WIFI_EVENT_STAMODE_GOT_IP:
#if DEBUG
            Serial.println("WiFi station got IP");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
#endif
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
#if DEBUG
            Serial.println("WiFi lost connection");
            webSocket.broadcastTXT("WRONG_USER_PASSOWRD");
#endif            
            break;
        case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
#if DEBUG        
            Serial.println("WiFi AP mode connected with STA active");
            Serial.println(WiFi.localIP());
            Serial.println((IPAddress)WiFi.softAPIP());
#endif           

            break;
        case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
#if DEBUG
            Serial.println("WiFi AP mode disconnected");
#endif            
            break;
   }
}

// =================================================================================================
// Function : Command_AP_STA_Config
// Description: This function set the configuration of AP or STA 
// =================================================================================================
void Command_AP_STA_Config(String modem_mode_ssid_pswd)
{
    int Number_Of_Token = 0;
    StringTokenizer tokens_mode_ssid_pswd(modem_mode_ssid_pswd, "=");
    while(tokens_mode_ssid_pswd.hasNext()){
        // prints the next token in the string
        mode_ssid_pswd[Number_Of_Token] = tokens_mode_ssid_pswd.nextToken();
#if DEBUG
         Serial.println(mode_ssid_pswd[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    if(mode_ssid_pswd[1]== "AP")
    ;
    else if(mode_ssid_pswd[1]== "STA")
     {
     
	temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
	webSocket.broadcastTXT(temp);
	temp = "";
	delay(1000);
      WiFi.disconnect();
      delay(2000);
      WiFi.mode(WIFI_STA);
      WiFi.begin(mode_ssid_pswd[2].c_str(), mode_ssid_pswd[3].c_str());
        while (WiFi.status() != WL_CONNECTED) {
    delay(500);
#if DEBUG 
    Serial.print(".");
    Serial.println(WiFi.status());
#endif
 }
      delay(2000);
     }
   ESP.reset();
}

// =================================================================================================
// Function : Command_Load_Description
// Description: This function send load description
// =================================================================================================
void Command_Load_Description(String load_number)
{
    
   //client.print("HOMEname=HomeID=DEVICE1=DEVICE2=DEVICE3=DEVICE4");
}

// =================================================================================================
// Function : Command_Load_Description
// Description: This function configure the load
// =================================================================================================
void Command_Load_Configuration(String Config)
{
    
   //TODO
}

// =================================================================================================
// Function : Command_ws2812_status
// Description: This function read the ws2812 parameters
// =================================================================================================
void Command_ws2812_status()
{    
   webSocket.broadcastTXT(current_state);
}

// =================================================================================================
// Function : Command_Factory_Reset
// Description: This function do factory reset
// =================================================================================================
void Command_Factory_Reset(String factory_reset)
{
#if DEBUG
  Serial.println("+ Command_Factory_Reset");
#endif
    if(factory_reset == "reset")
    {
      for (int i = 0; i < EEPROM_SIZE; i++)
        EEPROM.write(i, 0);
      EEPROM.commit();
      delay(1000);
      config_init_default();//reset the config values both AP and STA??
    }

    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
    delay(1000);
    //WiFi.disconnect();
    
    ESP.reset();
#if DEBUG
  Serial.println("- Command_Factory_Reset");
#endif
}


// ***************************************************************************
// EEPROM helper
// ***************************************************************************
String readEEPROM(int offset, int len) {
  String res = "";
  for (int i = 0; i < len; ++i)
  {
    res += char(EEPROM.read(i + offset));
  }
  return res;
#if DEBUG
      Serial.print(res);
#endif
}

void writeEEPROM(int offset, int len, String value) {
  for (int i = 0; i < len; ++i)
  {
    if (i < value.length()) {
      EEPROM.write(i + offset, value[i]);
    } else {
      EEPROM.write(i + offset, NULL);
    }
  }
}


// ***************************************************************************
// Saved state handling
// ***************************************************************************
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

long convertSpeed(int mcl_speed) {
  long ws2812_speed = mcl_speed * 256;
  ws2812_speed = SPEED_MAX - ws2812_speed;
  if (ws2812_speed < SPEED_MIN) {
    ws2812_speed = SPEED_MIN;
  }
  if (ws2812_speed > SPEED_MAX) {
    ws2812_speed = SPEED_MAX;
  }
  return ws2812_speed;
}
void setModeByStateString(String saved_state_string) {
  String str_mode = getValue(saved_state_string, '|', 1);
  mode = static_cast<MODE>(str_mode.toInt());
  String str_ws2812fx_mode = getValue(saved_state_string, '|', 2);
  ws2812fx_mode = str_ws2812fx_mode.toInt();
  String str_ws2812fx_speed = getValue(saved_state_string, '|', 3);
  ws2812fx_speed = str_ws2812fx_speed.toInt();
  String str_brightness = getValue(saved_state_string, '|', 4);
  brightness = str_brightness.toInt();
  String str_red = getValue(saved_state_string, '|', 5);
  main_color.red = str_red.toInt();
  String str_green = getValue(saved_state_string, '|', 6);
  main_color.green = str_green.toInt();
  String str_blue = getValue(saved_state_string, '|', 7);
  main_color.blue = str_blue.toInt();

  if(mode == 0) //OFF situation
  {
    ws2812fx.setColor(0,0,0);
    ws2812fx.setMode(FX_MODE_STATIC);
  }
  else
  {
    ws2812fx.setColor(main_color.red, main_color.green, main_color.blue); //Set all color
    if(brightness > 255)
      brightness = 255;
    ws2812fx.setBrightness(brightness); 
    ws2812fx.setSpeed(convertSpeed(ws2812fx_speed));
    ws2812fx.setMode(ws2812fx_mode);
  }
  ws2812fx.start();
}

void shortKeyPress() {
  if (buttonState == false) {
    setModeByStateString(BTN_MODE_SHORT);
    buttonState = true;
  } else {
    mode = OFF;
    buttonState = false;
  }
}

// called when button is kept pressed for less than 2 seconds
void mediumKeyPress() {
  setModeByStateString(BTN_MODE_MEDIUM);
}

// called when button is kept pressed for 2 seconds or more
void longKeyPress() {
  //setModeByStateString(BTN_MODE_LONG);
  Command_Factory_Reset("reset");
}

void button() {
  if (millis() - keyPrevMillis >= keySampleIntervalMs) {
    keyPrevMillis = millis();

    byte currKeyState = digitalRead(FACTORY_RESET_PIN);

    if ((prevKeyState == HIGH) && (currKeyState == LOW)) {
      // key goes from not pressed to pressed
      KeyPressCount = 0;
    }
    else if ((prevKeyState == LOW) && (currKeyState == HIGH)) {
      if (KeyPressCount < longKeyPressCountMax && KeyPressCount >= mediumKeyPressCountMin) {
        mediumKeyPress();
      }
      else {
        if (KeyPressCount < mediumKeyPressCountMin) {
          shortKeyPress();
        }
      }
    }
    else if (currKeyState == LOW) {
      KeyPressCount++;
      if (KeyPressCount >= longKeyPressCountMax) {
        longKeyPress();
      }
    }
    prevKeyState = currKeyState;
  }
}


// =================================================================================================
// Description: Handles all WS2812 commands. First check initial setup is working or not
// =================================================================================================
void Command_WS2812(String Ws2812_data)
{
   int Number_Of_Token = 0;
    StringTokenizer tokens(Ws2812_data, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        WS2812_CODE[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(WS2812_CODE[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    mode = (MODE)WS2812_CODE[1].toInt();  
    ws2812fx_mode = (int)WS2812_CODE[2].toInt();
    ws2812fx_speed = (int)WS2812_CODE[3].toInt();
    ws2812fx_speed = constrain(ws2812fx_speed, 0, 255);
    brightness = (int)WS2812_CODE[4].toInt();
    main_color.red = (int)WS2812_CODE[5].toInt();
    main_color.green = (int)WS2812_CODE[6].toInt();
    main_color.blue = (int)WS2812_CODE[7].toInt();

  if(mode == 0) //OFF situation
  {
    //ws2812fx.setColor(0,0,0);
    ws2812fx.setColor(0x0);
    ws2812fx.setMode(FX_MODE_STATIC);
  }
  else
  {
    ws2812fx.setColor(main_color.red, main_color.green, main_color.blue); //Set all color
    if(brightness > 255)
      brightness = 255;
    ws2812fx.setBrightness(brightness); 
    ws2812fx.setSpeed(convertSpeed(ws2812fx_speed));
    ws2812fx.setMode(ws2812fx_mode);
  }
  ws2812fx.start();
    WS2812_CODE[0]= ""; WS2812_CODE[1]= ""; WS2812_CODE[2]= "";WS2812_CODE[3]= "";WS2812_CODE[4]= "";WS2812_CODE[5]= "";WS2812_CODE[6]= "";WS2812_CODE[7]= ""; //Make the string NULL
    
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
}

void Command_Set_Led_Count(String Ws2812_led_count)
{
     int Number_Of_Token = 0;
    StringTokenizer tokens(Ws2812_led_count, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        WS2812_LED_CNT[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(WS2812_LED_CNT[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    led_count = (MODE)WS2812_LED_CNT[1].toInt(); //TODO it will affect from next reboot or shall I reinitalize the WS object
    ws2812fx.setLength(led_count);
    //EEPROM.write(LED_CNT,led_count); //LED has two byte.
    writeEEPROM(LED_CNT, 3, WS2812_LED_CNT[1]);
    EEPROM.commit();
    WS2812_LED_CNT[0]=""; WS2812_LED_CNT[1]="";
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
}

void Command_Set_Led_brightness(String Ws2812_led_brightness)
{
     int Number_Of_Token = 0;
    StringTokenizer tokens(Ws2812_led_brightness, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        WS2812_LED_BRT[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(WS2812_LED_BRT[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    brightness = (MODE)WS2812_LED_BRT[1].toInt(); //TODO it will affect from next reboot or shall I reinitalize the WS object
    if(brightness > 255)
      brightness = 255;
    ws2812fx.setBrightness(brightness);
    
    WS2812_LED_BRT[0]= ""; WS2812_LED_BRT[1]= "";//Make the string NULL
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
}

void Command_Set_Led_Speed(String Ws2812_led_speed)
{
     int Number_Of_Token = 0;
    StringTokenizer tokens(Ws2812_led_speed, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        WS2812_LED_SPD[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(WS2812_LED_SPD[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    ws2812fx_speed = (MODE)WS2812_LED_SPD[1].toInt(); //TODO it will affect from next reboot or shall I reinitalize the WS object
    ws2812fx_speed = constrain(ws2812fx_speed, 0, 255);
    ws2812fx.setSpeed(convertSpeed(ws2812fx_speed));
    WS2812_LED_SPD[0]= ""; WS2812_LED_SPD[1]= "";//Make the string NULL
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
}

void Command_Set_Initialized(String device_initialized)
{
     int Number_Of_Token = 0;
    StringTokenizer tokens(device_initialized, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        DEVICE_INIT[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(DEVICE_INIT[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    EEPROM.write(INITIALIZED,DEVICE_INIT[1].toInt());
    EEPROM.commit();    
    DEVICE_INIT[0]= ""; DEVICE_INIT[1]= ""; //Make the string NULL
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WS2812_SENT=OK" ;
    webSocket.broadcastTXT(temp);
    temp = "";
}

// =================================================================================================
// Function : Command_Parsing
// Description: This function Parse the command and call the respective function
// =================================================================================================
void Command_Parsing()
{   
    int Number_Of_Token = 0;
    StringTokenizer tokens(TxRx_string, "::");
    while(tokens.hasNext()){
        // prints the next token in the string
        Parsed_data[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(Parsed_data[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    if(Parsed_data[1] != "CMD")
    {
      temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=WRONG_COMMAND" ;
      webSocket.broadcastTXT(temp);
      temp = "";
#if DEBUG      
      Serial.println("Wrong Command");
#endif      
    }
    else
    {
      if(Parsed_data[2] == "CONF" || Parsed_data[2] == "CONT" || Parsed_data[2] == "STATUS")
      {
        if(Parsed_data[3] == "000")
          ;//Command_AP_STA_Config(Parsed_data[4]);
        else if(Parsed_data[3] == "001")
          ;//Command_AP_STA_Config(Parsed_data[4]);
        else if(Parsed_data[3] == "004")
          ;//Command_Load_Configuration(Parsed_data[4]);
        else if(Parsed_data[3] == "003")
          Command_AP_STA_Config(Parsed_data[4]);
        else if(Parsed_data[3] == "005")
          Command_Set_Initialized(Parsed_data[4]);
        else if(Parsed_data[3] == "300")
          Command_WS2812(Parsed_data[4]);
        else if(Parsed_data[3] == "301")
          Command_ws2812_status();
        else if(Parsed_data[3] == "302")
          Command_Set_Led_Count(Parsed_data[4]);
        else if(Parsed_data[3] == "303")
          Command_Set_Led_brightness(Parsed_data[4]);
        else if(Parsed_data[3] == "304")
          Command_Set_Led_Speed(Parsed_data[4]);
        else if(Parsed_data[3] == "999")
          Command_Factory_Reset(Parsed_data[4]);
      }
      Parsed_data[0]="";Parsed_data[1]="";Parsed_data[2]="";Parsed_data[3]="";Parsed_data[4]="";
    }
}

//==================================================================================================
//  Function: Setup
//       This function setup EEPROM for setting default value
//       along with GPIO pin to check Factory reset and Serial port with 9600 baudrate for debug or
//        connected light with micro controller.
// =================================================================================================
void setup()
{
  flipper.attach(60, flip);
  timeClient.begin();
  // initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect.
  }

    //WiFi.mode(WIFI_AP_STA); //Set to AP STA mode by default.
    //Make the EEPROM available so that we can read the default values
    EEPROM.begin(EEPROM_SIZE); //Initalize the EEPROM
    //Check the Factory reset Pin is pressed?
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
    // start webSocket server
    //webSocket.begin();
    //webSocket.onEvent(webSocketEvent);
    //WiFi.persistent(false); //making WiFi.disconnect does not erase the wifi ssid and password from memory.
        
    if(!digitalRead(FACTORY_RESET_PIN))
    {
#if DEBUG
      Serial.print("Inside the Factory reset process");
      Serial.println();
#endif
      //Do factory reset
      Command_Factory_Reset("reset");

    }
    //first check the device is already initialized or it is the first boot?
    if(1 == EEPROM.read(INITIALIZED))
    {
        WiFi.mode(WIFI_STA);
#if DEBUG
      //already initialized so just go to loop
      Serial.print("already initialized so just go to loop");
      Serial.println();
#endif
     //Setup call back for events
      WiFi.onEvent(WiFiEvent);
      WiFi.begin();
        while (WiFi.status() != WL_CONNECTED) {
	delay(500);
#if DEBUG
	Serial.print(".");
	Serial.println(WiFi.status());
#endif
    }
    String temp = "OK";
    webSocket.begin();
    webSocket.broadcastPing(temp);
    webSocket.onEvent(webSocketEvent); 
#if DEBUG 
      Serial.printf("MAC address = %s\n", WiFi.softAPmacAddress().c_str());
      Serial.printf("Connected, mac address: %s\n", WiFi.macAddress().c_str());
#endif
	WiFi.mode(WIFI_AP_STA); //Set to AP STA mode by default.
      String temp_ssid = WiFi.macAddress().c_str();
      strcpy(ap_ssid+12, temp_ssid.c_str()); 
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      //WiFi.softAP((char *)ap_ssid, ap_password,1,1); //for hidding the AP mode ssid
      WiFi.softAP((char *)ap_ssid, ap_password);
      delay(1000);
      //WiFi.begin(); //make sure to enable the STA mode along with AP mode.
#if DEBUG 
      temp_ssid = String((char*)ap_ssid);
      Serial.println("SoftAP***************SSID:"+ temp_ssid);
      IPAddress myIP = WiFi.softAPIP();
      Serial.print("AP IP address: ");
      Serial.println(myIP);
#endif
      temp_ssid ="";

      //WiFi.setAutoConnect(true);

      //TODO read all the value from EEPROM and initialize the WS2812.
      ws2812fx.init();
      ws2812fx.setBrightness(brightness); 
      ws2812fx.setSpeed(convertSpeed(ws2812fx_speed));
      //ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
      ws2812fx.setColor(main_color.red, main_color.green, main_color.blue);
      led_count = (int)readEEPROM(LED_CNT, 3).toInt();//EEPROM.read(LED_CNT);
#if DEBUG
      Serial.print(led_count);
#endif
      ws2812fx.setLength(led_count);
      ws2812fx.start();
  
      String saved_state_string = readEEPROM(256, 32);
      String chk = getValue(saved_state_string, '|', 0);
      if (chk == "WS2") {
        setModeByStateString(saved_state_string);
      }
      sprintf(last_state, "WS2|%2d|%3d|%3d|%3d|%3d|%3d|%3d", mode, ws2812fx_mode, ws2812fx_speed, brightness, main_color.red, main_color.green, main_color.blue);
    }
    else //it is the HW reset due to FACTORY_RESET_PIN , we are ready for initializing all the default values.
    {
        WiFi.mode(WIFI_AP); //Set to AP mode if not configured.
	delay(2000);
      //WiFi.persistent(false); //making WiFi.disconnect does not erase the wifi ssid and password from memory.
#if DEBUG
      Serial.print("Inside setting default value");
      Serial.println();
#endif
    String temp = "OK";
    webSocket.begin();
    webSocket.broadcastPing(temp);
    webSocket.onEvent(webSocketEvent); 
      //Make everything zero before setting default value
      for (int i = 0; i < EEPROM_SIZE; i++)
        EEPROM.write(i, 0);
      EEPROM.commit();

      String temp_ssid = WiFi.macAddress().c_str();
      strcpy(ap_ssid+12, temp_ssid.c_str()); 
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      //WiFi.softAP((char *)ap_ssid, ap_password,1,1); //for hidding the AP mode ssid
      WiFi.softAP((char *)ap_ssid, ap_password);
      delay(1000);
      WiFi.begin(); //only AP mode.
#if DEBUG 
      temp_ssid = String((char*)ap_ssid);
      Serial.println("SoftAP***************SSID:"+ temp_ssid);
      IPAddress myIP = WiFi.softAPIP();
      Serial.print("AP IP address: ");
      Serial.println(myIP);
#endif
      // set Mode, set Brightness, set color and speed.
      sprintf(last_state, "WS2|%2d|%3d|%3d|%3d|%3d|%3d|%3d", mode, ws2812fx_mode, ws2812fx_speed, brightness, main_color.red, main_color.green, main_color.blue);
      writeEEPROM(256, 32, last_state); // 256 --> last_state (reserved 32 bytes)
      //We are ready to set the initialized bit
      //EEPROM.write(INITIALIZED,1); //initialize only when all setting is done.command is been provided to do this.
      EEPROM.write(LED_CNT,20);
      EEPROM.commit();
      //WiFi.disconnect();//disconnect from previous AP and make ssid pswd null.. make sure to make persistent(false)
     // ESP.reset();

    // start webSocket server again
    //webSocket.begin();
    //webSocket.onEvent(webSocketEvent);

           //TODO read all the value from EEPROM and initialize the WS2812.
      ws2812fx.init();
      ws2812fx.setBrightness(brightness); 
      ws2812fx.setSpeed(convertSpeed(ws2812fx_speed));
      //ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
      ws2812fx.setColor(main_color.red, main_color.green, main_color.blue);
      led_count = (int)readEEPROM(LED_CNT, 3).toInt();//EEPROM.read(LED_CNT);
#if DEBUG
      Serial.print(led_count);
#endif
      ws2812fx.setLength(led_count);
      ws2812fx.start();

   }
#if DEBUG
    Serial.print("starting wifi multicast");
    Serial.println();
#endif
    udp.beginMulticast(WiFi.localIP(),ipMulti,6777);
    udp.beginPacketMulticast(ipMulti,6777,WiFi.localIP());
    udp.write(WiFi.softAPmacAddress().c_str());
    udp.endPacket();
    // For OTA upgrade
    String hostname("Connected-OTA-");
    hostname += String(ESP.getChipId(), HEX);
    ArduinoOTA.setHostname((const char *)hostname.c_str());
    ArduinoOTA.begin();
}

void loop()
{
 //button(); 
 int packetSize = udp.parsePacket();
  if (packetSize)
  {
    // read the packet into packetBufffer
    udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    // send a reply, to the IP address and port that sent us the packet we received
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    temp = "MAC=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=" ;
    udp.write(temp.c_str());
    udp.endPacket();
    temp = "";
  }
  webSocket.loop();
  ws2812fx.service();
  ArduinoOTA.handle();
  timeClient.update();
 // Check for state changes
  sprintf(current_state, "WS2|%2d|%3d|%3d|%3d|%3d|%3d|%3d", mode, ws2812fx.getMode(), ws2812fx_speed, brightness, main_color.red, main_color.green, main_color.blue);
 
  if (strcmp(current_state, last_state) != 0) {
    strcpy(last_state, current_state);
    time_statechange = millis();
    state_save_requested = true;
    //setModeByStateString(current_state);
  }
  if (state_save_requested && time_statechange + timeout_statechange_save <= millis()) {
    time_statechange = 0;
    state_save_requested = false;
    writeEEPROM(256, 32, last_state); // 256 --> last_state (reserved 32 bytes)
    EEPROM.commit();
  }
}
