#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>

const char* ssid ="Sarel-EXT2G";
const char* password ="10203040";
WiFiClient client;
void wifi_Setup() {
  Serial.println("wifiSetup");
  WiFi.begin(ssid,password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Attempting to connect to network...");
    delay(100);
  }
  Serial.println("Connected to network");
}
String GetData() {
  //Serial.print("here statuscode");
  String ret = "Error";
  HTTPClient http;
  String dataURL = "";
  dataURL+="/?btnPressed="+btnPressed;
  dataURL+="&EmptyBed="+EmptyBed;
   http.begin(client,"http://10.0.0.6:3001/index"+dataURL);
   int httpCode = http.GET();
  // Serial.println(httpCode);
   if (httpCode == HTTP_CODE_OK) {
    // Serial.print("HTTP statuscode ok");
    // Serial.print("HTTP response code ");
     String Res = http.getString();
    //  Serial.println(Res);
    ret = Res;
   }
  // Serial.print("HTTP statuscode not ok");
   http.end();

  return ret;
}