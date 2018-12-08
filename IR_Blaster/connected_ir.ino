//==================================================================================================
//                IR Blaster
//==================================================================================================
#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <EEPROM.h>  //For EEEPROM library inclusion
#include <ESP8266WiFi.h> //For ESP wifi library
#include <WiFiUdp.h>      // To enable UDP over wifi
#include <StringTokenizer.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <string.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>

extern "C"{
#include "spi_flash.h"
#include "ets_sys.h"
}

#define DEBUG 1
#define EEPROM_SIZE 512
#define FACTORY_RESET_PIN 2
#define UDP_PORT  6755
#define TCP_PORT  9999

#define LED_COUNT 20
#define LED_PIN D1


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
  NO_OF_RELAY = 1,
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
String IRCODE[3];
String DEVICE_INIT[2];
String mode_ssid_pswd[4];

IRsend irsend(4);  // An IR LED is controlled by GPIO pin 4 (D2)



WebSocketsServer webSocket = WebSocketsServer(TCP_PORT);

WiFiUDP ntpUDP;
//NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);
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
  //String formattedTime = timeClient.getFormattedTime();
  temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=" + "Hello";
  webSocket.broadcastTXT(temp);
  temp = "";
//  formattedTime = "";
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
     
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=IR_SENT=OK" ;
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

    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=IR_SENT=OK" ;
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
// =================================================================================================
// Function :   
// Description: This function Parse the command and call the respective function
// =================================================================================================
void Command_IRBlasting(String IRcode)
{
    unsigned short int Number_Of_Token = 0;
    String temp_data = "";
    unsigned int temp_data_int = 0;
    uint16_t *temp_irdata= NULL;
    unsigned short int kHz = 0;
    StringTokenizer tokens(IRcode, "=");
    while(tokens.hasNext()){
        // prints the next token in the string
        IRCODE[Number_Of_Token] = tokens.nextToken();
#if DEBUG
         Serial.println(IRCODE[Number_Of_Token]);
#endif
        Number_Of_Token ++;   
    }
    Number_Of_Token = IRCODE[3].toInt();
    kHz = (unsigned short int)IRCODE[1].toInt();
    temp_irdata = new uint16_t[Number_Of_Token];
    if(!temp_irdata)  
    {     
#if DEBUG     
      Serial.println("No memory to allocate ... you are going to die");      
#endif
      temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=IR_SENT=ERROR" ;      
      webSocket.broadcastTXT(temp);
      temp = "";
    } else 
    {
        StringTokenizer tokens_ircode_final(IRCODE[2], ",");
        Number_Of_Token = 0;
        while(tokens_ircode_final.hasNext()){
            temp_data_int = tokens_ircode_final.nextToken().toInt();
            temp_irdata[Number_Of_Token]=((unsigned int )temp_data_int*1000 /kHz);
            Number_Of_Token++;
        }
        irsend.sendRaw(temp_irdata, Number_Of_Token , kHz); 
        delete []temp_irdata;
        temp_irdata = NULL;
    }
    IRCODE[0]= ""; IRCODE[1]= ""; IRCODE[2]= ""; //Make the string NULL
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=IR_SENT=OK" ;
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
    temp = "ESP_NODE=" +WiFi.macAddress()+ "=" + WiFi.softAPmacAddress()+ "=IR_SENT=OK" ;
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
        else if(Parsed_data[3] == "003")
          Command_AP_STA_Config(Parsed_data[4]);
        else if(Parsed_data[3] == "005")
          Command_Set_Initialized(Parsed_data[4]);
        else if(Parsed_data[3] == "200")
          Command_IRBlasting(Parsed_data[4]);
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
//  timeClient.begin();
  // initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect.
  }

    //Make the EEPROM available so that we can read the default values
    EEPROM.begin(EEPROM_SIZE); //Initalize the EEPROM
    //Check the Factory reset Pin is pressed?
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
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

     }else //it is the HW reset due to FACTORY_RESET_PIN , we are ready for initializing all the default values.
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

   }


    udp.beginMulticast(WiFi.localIP(),ipMulti,6777);
    udp.beginPacketMulticast(ipMulti,6777,WiFi.localIP());
    udp.write(WiFi.softAPmacAddress().c_str());
    udp.endPacket();
    // For OTA upgrade
    String hostname("Connected-OTA-");
    hostname += String(ESP.getChipId(), HEX);
    ArduinoOTA.setHostname((const char *)hostname.c_str());
    ArduinoOTA.begin();
    irsend.begin();
}

void loop()
{
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
//  timeClient.update();

}
