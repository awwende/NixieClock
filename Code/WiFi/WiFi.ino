#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
extern "C"{
  #include "user_interface.h"
}

ESP8266WebServer webserver(80);

unsigned int localPort = 2390;      // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
char ssid[50];  //your network SSID (name)
char pw[50];    //your network password
byte ip[4] = {0}; //ntp server IP
int offset = 0;   //time zone

boolean wifiConnected=false,tryingToConnect,validID,validIP,validOffset;
String HTTP_req, st;
WiFiUDP udp;

void setup(){
  Serial.begin(9600);
  EEPROM.begin(110);  //reserve 110 bytes of memory for EEPROM
  readMemory();
  //get list of visable network IDs (used in config webpage, handleRoot())
  byte n = WiFi.scanNetworks();
  st = "<label>SSID</label><select name=\"ssid\">";
  st += "<option>Choose a Network</option>";
  for (int i = 0; i < n; ++i){
    // Print SSID and RSSI for each network found
    st += "<option value=";
    st += String(i);
    st += ">";
    st += WiFi.SSID(i);
    st += " (";
    st += String(WiFi.RSSI(i));
    st += "dBm)";
    st += "</option>";
  }
  st += "</select><br>"; 
  /////////////////////////////////////////////////////////////////////////
  WiFi.mode(WIFI_AP_STA);  //set up radio as both access point and station
  WiFi.softAP("Nixie Clock", "wende1098");  //Start Access Point to host config webpage (192.168.4.1)
  if(validID && validIP && validOffset)startWiFi(); //try to connect to wifi
  //setup web server
  webserver.on("/", handleRoot);
  webserver.on("/update", handleUpdate);
  webserver.onNotFound(handleNotFound);
  webserver.begin();
}

void loop(){ 
  webserver.handleClient();
  yield();
  check_serial();
}

void handleRoot() {   //webpage to be displayed at 192.168.4.1 or DHCP address when conencted to AP
  String tz = "<select name=\"name=tz\">";
  tz += "<option value=0>Choose Time Zone</option>";
  tz += "<option value=4>Eastern</option>";
  tz += "<option value=5>Central</option>";
  tz += "<option value=6>Mountain</option>";
  tz += "<option value=7>Pacific</option></select>";
        
  String htmlDoc = "<!DOCTYPE html>";
  htmlDoc += "<html>";
  htmlDoc += "<head>";
  htmlDoc += "<style type=\"text/css\">ul{ list-style: none;} label{ display: inline-block; width: 80px; margin: 3px 0;} button{ width 120px; margin-top: 10px;}</style>";
  htmlDoc += "<title>Clock Settings</title>";
  htmlDoc += "</head>";
  htmlDoc += "<body>";
  htmlDoc += "<fieldset><ul><li>Fill in the information below, and press submit.</li>";
  
  htmlDoc += "<form method=\"post\" action=\"/update\" id=\"clock_info\">";
  htmlDoc +="<li>"+st+"</li><li><label>Password</label><input type=\"password\" name='pass'/></li><li><label>Server IP</label><input type=\"text\" name=\"serverIP\"/></li>";
  htmlDoc +="<li><label>Time Zone</label>"+tz+"</li><li><button type=\"submit\">Submit</button></li></form>";
  htmlDoc += "<br>";
  htmlDoc += "<div style=\"width:400px;\"><div style=\"float: left\">";
  htmlDoc += "<form method=\"post\" action=\"/update\"><li><input type=\"submit\" name=\"option\" value=\"Reset Clock\"></li></form></div></div>";
  htmlDoc += "<div style=\"width:400px;\"><div style=\"float: right; width: 300px\">";
  htmlDoc += "<form method=\"post\" action=\"/update\"><li><input type=\"submit\" name=\"option\" value=\"Clear Memory\"></li></ul></fieldset></form></div>";

  htmlDoc += "</body>";
  htmlDoc += "</html>";
  webserver.send(200, "text/html", htmlDoc);
}

void handleNotFound() {
  webserver.send(404, "text/plain", "Page not found ...");
}

void handleUpdate() { //Get data from root page when submitted
  String str, mySSID, myPass;
  byte dotLocation[3];  //location of '.' in IP address form
  if (webserver.args() > 0 ) {
    for ( uint8_t i = 0; i < webserver.args(); i++ ) {
      if(webserver.argName(i) == "ssid"){ //Network SSID
        mySSID = WiFi.SSID(webserver.arg(i).toInt());
        mySSID.toCharArray(ssid,mySSID.length());
        writeMemory(0,mySSID);  //write SSID to memory (starting at address 0)
        str += "Settings are being updated.\n\nThe clock will reset with the new settings momentarily.\r\n";
      }
      else if(webserver.argName(i) == "pass"){  //Network Password
        myPass = webserver.arg(i);
        myPass.toCharArray(pw,myPass.length());
        writeMemory(50,myPass); //write password to memory (starting at address 50)
      }
      else if(webserver.argName(i) == "serverIP"){  //Server IP address
        dotLocation[0] = webserver.arg(i).indexOf('.');
        dotLocation[1] = webserver.arg(i).indexOf('.',dotLocation[0]+1);
        dotLocation[2] = webserver.arg(i).indexOf('.',dotLocation[1]+1);
        ip[0] = webserver.arg(i).substring(0,dotLocation[0]).toInt();
        ip[1] = webserver.arg(i).substring(dotLocation[0]+1,dotLocation[1]).toInt();
        ip[2] = webserver.arg(i).substring(dotLocation[1]+1,dotLocation[2]).toInt();
        ip[3] = webserver.arg(i).substring(dotLocation[2]+1).toInt();
        //save NTP server IP to memory
        EEPROM.write(100,ip[0]);
        EEPROM.write(101,ip[1]);
        EEPROM.write(102,ip[2]);
        EEPROM.write(103,ip[3]);
      }
      else if(webserver.argName(i) == "name%3Dtz"){ //Time Zone
        offset = webserver.arg(i).toInt() + 1;
        EEPROM.write(104,offset); //Save time zone to memory
        Serial.println("RESET");  //reset clock
      }
      else if(webserver.argName(i) == "option"){  //other instructions
        if(webserver.arg(i) == "Reset+Clock"){  //...reset the clock
          str = "Resetting Clock ...";
        }
        else if(webserver.arg(i) == "Clear+Memory"){  //erase all memory of network
          str = "All memory of settings have been erased ...";
          for(i=0;i<105;i++){
            EEPROM.write(i,255);
          }
        }
        Serial.println("RESET");  //tell ATmega328 to reset
      }
      else{
        str = "There was an error, sorry about that. Go back and try again.";
      }
    }
    readMemory(); //pull settings from memory
  }
  else{
    str = "Nothing to update ...\r\n";
  }
  webserver.send(200, "text/plain", str.c_str());
  EEPROM.commit();  //apply changes to memory
  system_restart(); //reset ESP8266
}

String check_serial(){
  String message = "";
  if(Serial.available()){   //Read messages
    delay(100);
    while(Serial.available()){
      yield();
      message += char(Serial.read());
    }
    if(message.startsWith("ST")){   //WiFi Status
      byte wifiStatus=0;
      if(WiFi.status() == WL_CONNECTED) wifiStatus = 8; //WiFi connected
      else{ //Not Connected
        if(tryingToConnect){  //tell ATmega328 to wait, currently trying to connect
          wifiStatus = 32;
        }
        else{   //send status (is there SSID to connect to? Is there an IP to get the time from? Is the time zone configured?)
          if(validID) wifiStatus += 1;
          if(validIP) wifiStatus += 2;
          if(validOffset) wifiStatus += 4;
        }
      }
      message = "ACK " + String(wifiStatus);  //message to be sent
    }
    else if(message.startsWith("BG")){  //Start WiFi
      message = "ACK ";
      if(!tryingToConnect){ //start wifi if it isn't already trying to
        if(WiFi.status() == WL_CONNECTED) message += "8"; //wifi connected
        else{
          Serial.println("ACK 9");  //starting attempt to connect
          message += "16";
          startWiFi();
        }
      }
      else{ //tell ATmega328, it's already connecting
        message += "32";
      }
    }
    else if(message.startsWith("GT")){    //Get time from server
      message = "ACK ";
      IPAddress timeServer(ip[0], ip[1], ip[2], ip[3]); // time.nist.gov NTP server
      sendNTPpacket(timeServer); // send an NTP packet to a time server
      delay(250);
      int cb = udp.parsePacket();
      if(!cb){
        message += "64";  //no packet received
      }
      else{
        message += sendTime(); //read time from server response
      }
    }
    else if(message.startsWith("RS")){  //reset ESP8266
      system_restart();
    }
    else if(message.startsWith("ACK")){
      message = "ACK";
    }
    else{
      message = "NR";
    }
    Serial.println(message);
  }
  return message;
}
String sendTime(){
  String currentTime="";
  udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  unsigned long secsSince1900 = highWord << 16 | lowWord; // this is NTP time (seconds since Jan 1 1900):
  const unsigned long seventyYears = 2208988800UL;        // now convert NTP time into everyday time:
  unsigned long epoch = secsSince1900 - seventyYears;     // subtract seventy years:


  // print the hour, minute and second:
  byte hour = (epoch  % 86400L) / 3600;
  
  if(hour < offset) hour += 24 - offset;
  else if(hour == offset) hour = 0;
  else hour -= offset;
  
  if(hour < 10)currentTime += "0";
  currentTime += String(hour) + ":"; // print the hour (86400 equals secs per day)
  if ( ((epoch % 3600) / 60) < 10 ){  // In the first 10 minutes of each hour, we'll want a leading '0'
    currentTime += "0";
  }
  currentTime += String((epoch  % 3600) / 60) + ":"; // print the minute (3600 equals secs per minute)
  if ( (epoch % 60) < 10 ){  // In the first 10 seconds of each minute, we'll want a leading '0'
    currentTime += "0";
  }
  currentTime += String(epoch % 60); // print the seconds
  return currentTime;
}

unsigned long sendNTPpacket(IPAddress & address){
  //Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void writeMemory(byte startAddress, String message){
  byte i=0;
  for(i=0;i<message.length();i++)
  {
    EEPROM.write(i+startAddress,message.charAt(i));
  }
  EEPROM.write(message.length()+startAddress,255);
}

void readMemory(){
  for(byte i=0;i<50;i++){   //Read SSID
    char character = char(EEPROM.read(i));
    if(character == 255) break;
    else ssid[i] = character;
  }
  if(String(ssid).length()>2) validID = true;
  Serial.println("SSID:"+String(ssid));
  
  for(byte i=0;i<50;i++){   //Read Password
    char character = char(EEPROM.read(i+50));
    if(character == char(255)) break;
    else pw[i] = character;
  }
  Serial.println("PW:"+String(pw));
  Serial.print("IP:");
  for(byte i=0;i<4;i++){    //Read Server IP
    ip[i] = EEPROM.read(i+100);
    Serial.print(ip[i]);
    Serial.print(".");
  }
  if(!(ip[0]==255 && ip[0]==255 && ip[0]==255 && ip[0]==255)) validIP = true;
  offset = EEPROM.read(104);
  if(offset > 25) offset = 0;
  else validOffset = true;
  Serial.println("\nOffset:"+String(offset));
}

boolean startWiFi(){
  WiFi.begin(ssid,pw);
  unsigned int startTime = millis();
  while(WiFi.status() != WL_CONNECTED){ //wait to connect to wifi
    tryingToConnect = true;
    yield();
    check_serial();
    delay(50);
    if((millis()-startTime) > 10000) break; //timeout
  }
  tryingToConnect = false;
  if(WiFi.status() == WL_CONNECTED){
    udp.begin(localPort);
    wifiConnected = true;
  }
  else{
    byte wifiStatus = 0;
    if(validID) wifiStatus += 1;
    if(validIP) wifiStatus += 2;
    if(validOffset) wifiStatus += 4;
    wifiConnected = false;
  }
  return wifiConnected;
}
