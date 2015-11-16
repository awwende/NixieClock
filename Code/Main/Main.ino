#include <Time.h>
#include <avr/io.h>
#include <util/delay.h>;
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <EEPROM.h>

#define sr_le     PC0  //Latch Enable
#define sr_clk    PC1  //Clock
#define sr_dataIN PC2  //Data Input
#define sr_bl     PC3  //Blanking (0:OFF, 1:ON)
#define sr_pol    PC4  //Polarity (0:Invert 1: Normal)

String lastMessageReceived="";
String lastMessageSent="";
String ssid, pw;
byte ip[4];
int timeOffset;
boolean wifiConnected=false, updated=false;

void setup() 
{
  //Setup watchdog Timer
  WDTCSR |= (1<<WDCE)|(1<<WDE);
  WDTCSR = (1<<WDE)|(1<<WDP2) |(1<<WDP1);
  wdt_reset();

  Serial.begin(9600);
  //Wait 2 seconds for ESP8266 to Start
  for(byte i=0;i<8;i++)
  {
    wdt_reset();
    _delay_ms(250);
  }
  //Setup Shift Register
  DDRC |= (1<<sr_le) | (1<<sr_clk) | (1<<sr_dataIN) | (1<<sr_bl) | (1<<sr_pol);
  PORTC |= (1<<sr_bl) | (1<<sr_pol);
  
  //Serial.println("Started");
  wifi_setup();
  //Serial.println(String(hour())+":"+String(minute())+":"+String(second()));
}

void loop()
{
  byte myHour = hour();
  byte myMin = minute();
  byte mySec = second();
  byte digit[6] = {0};  //Array of clock digits
  check_serial();
  wdt_reset();
  if(myHour==23 && myMin==0 && mySec==0){ //Wait for WDT to reset microcontroller (Update the time)
    Serial.println("RS");
    while(1){}   
  }
  if(myHour > 12) myHour -= 12;
  else if(myHour == 0) myHour = 12;

  if(myHour<10)digit[5]=0;
  else digit[5] = myHour/10;
  digit[4] = myHour%10;
  
  if(myMin<10)digit[3]=0;
  else digit[3] = myMin/10;
  digit[2] = myMin%10;

  if(mySec<10)digit[1]=0;
  else digit[1] = mySec/10;
  digit[0] = mySec%10;
  
  for(byte i=0;i<6;i++){    //Display the digits on the nixies
    display_digit(i,digit[i]);
  }
}

String check_serial(){
  String message = "";
  byte failCount = 0;

  if(Serial.available()){
    _delay_ms(20);
    while(Serial.available()){
      message += char(Serial.read());
      _delay_ms(1);
    }
    if(message.startsWith("ACK ")){
      if(message.charAt(6) == ':'){
        byte hourTemp = (message.charAt(4)-48)*10 + (message.charAt(5)-48);
        byte minTemp  = (message.charAt(7)-48)*10 + (message.charAt(8)-48);
        byte secTemp  = (message.charAt(10)-48)*10 + (message.charAt(11)-48) + 1;
        setTime(hourTemp,minTemp,secTemp,1,1,1);
      }
    }
    else if(message.startsWith("RESET")){
      while(1){}
    }
  }
  else message = "";
  return message;
}

void sendMessage(String message){
  Serial.println(message);
  lastMessageSent = message;
  _delay_ms(50);
  wdt_reset();
}

void display_digit(byte digit, byte value){
  //When time is HH:MM:SS, digit flows left to right (including ":") valid 
  //and can be 0-7
  //value is the value to display on the element (0-9)
  const byte LUT[] = {2,7,8,9,6,5,4,3,0,1};   //Array takes 0-10, where 0-9 are the dec digit and 10 is the DP
  const byte srOffset[] = {32,43,53,0,11,22}; //How many 0s lead in the array to be clocked into Shift Register (SR)
  boolean srBuffer[65] = {0};
  if(value<10)  srBuffer[srOffset[digit]+LUT[value]+1]=1;      //Set SR bit
  PORTC &= ~(1<<sr_le);                          //Disable the SR latch
  for(byte i=64;i>0;i--){                        //Clock in all 64-bits of data to SR
    PORTC |= (1<<sr_clk);
    if(srBuffer[i]) PORTC |= (1<<sr_dataIN);
    else PORTC &= ~(1<<sr_dataIN);
    _delay_us(10);
    PORTC &= ~(1<<sr_clk);
    _delay_us(10);
  }
  PORTC |= (1<<sr_le);
  //digitalWrite(A0,HIGH);                          //Enable the SR latch to illuminate digit
  _delay_us(250);                                       //Wait 5ms, so that your eyes can actually see it
}

void wifi_setup(){
  String message="";
  display_digit(0,0);   //Configure NTP IP address
  while(!message.startsWith("ACK 7") && !message.startsWith("ACK 8")){ //Get Status
    wdt_reset();
    Serial.println("ST");
    _delay_ms(200);
    message = check_serial();
  }
  message = message.substring(4);
  if(message.startsWith("8")){  //Connected to WiFi
    display_digit(0,4);
    for(byte i=0;i<5;i++){
      while(!message.startsWith("ACK ")){ //Get Time
        wdt_reset();
        Serial.println("GT");
        _delay_ms(200);
        message = check_serial();
      }
      message = message.substring(4);
      if(message.charAt(2) == ':') break;
    }
    if(message.startsWith("64")){
      display_digit(0,6);
      for(byte i=0;i<4;i++){
        wdt_reset();
        _delay_ms(250);
      }
    }
  }
  else{ //Not Connected
    if(message.startsWith("6") || message.startsWith("7")){
      display_digit(0,1);
      while(!message.startsWith("ACK 9")){ //Get Status
        wdt_reset();
        Serial.println("BG");
        _delay_ms(200);
        message = check_serial();
      }
      display_digit(0,2);
      while(!message.startsWith("ACK 7") && !message.startsWith("ACK 8")){ //Get Status
        wdt_reset();
        Serial.println("ST");
        _delay_ms(200);
        message = check_serial();
      }
      display_digit(0,3);
      message = message.substring(4);
      if(message.startsWith("8")){
        display_digit(0,4);
        for(byte i=0;i<5;i++){
          while(!message.startsWith("ACK ")){ //Get Time
            wdt_reset();
            Serial.println("GT");
            _delay_ms(200);
            message = check_serial();
          }
          message = message.substring(4);
          if(message.charAt(2) == ':') break;
        }
        if(message.startsWith("64")){
          display_digit(0,6);
          for(byte i=0;i<4;i++){
            wdt_reset();
            _delay_ms(250);
          }
        }
      }
      else{
        display_digit(0,5);
        while(1){} //reset
      }
    }
  }
}

