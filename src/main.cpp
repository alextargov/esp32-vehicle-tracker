#include <Wire.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP_Mail_Client.h>

// RTC, GPS and WiFi credentials
RTC_DS1307 rtc;
TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

const char* ssid = "";
const char* password = "";

// Firebase credentials
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""
FirebaseData firebaseData;

// Email credentials and settings
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define EMAIL_SENDER "you@gmail.com"
#define EMAIL_SENDER_PASSWORD "your_password"
#define EMAIL_RECIPIENT ""

// GPS pins
#define RX2 16
#define TX2 17

SMTPData smtpData;
ESP_Mail_Client mailClient;

void setup() {  
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, RX2, TX2);

  #ifndef ESP8266
    while (!Serial);
  #endif

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  }

  // Initialize Firebase
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  // Initialize Email
  mailClient.setLogin(SMTP_HOST, SMTP_PORT, EMAIL_SENDER, EMAIL_SENDER_PASSWORD);
  mailClient.setSender("ESP32", EMAIL_SENDER);
  mailClient.setPriority("High");
  mailClient.setSubject("Coordinates Alert");
}

static uint32_t lastCheck = uint32_t(0);

void loop() {
  DateTime now = rtc.now();

  bool newData = false;
  // Check if 30 seconds have passed
  if (now.unixtime() - lastCheck >= uint32_t(60 * 0.5)) {
    lastCheck = rtc.now().unixtime();
    Serial.println("30sec have passed. Executing task.");

    while (SerialGPS.available()){
      char c = SerialGPS.read();
      // Serial.write(c); // uncomment this line if you want to see the GPS data flowing
      if (gps.encode(c)) // Did a new valid sentence come in?
        newData = true;
    }
    
    if (newData) {
      double currentLat = gps.location.lat();
      double currentLng = gps.location.lng();

      Serial.println("Positions:");
      Serial.println(String(currentLat, 7));
      Serial.println(String(currentLng, 7));
      Serial.println(String(gps.satellites.value()));
      Serial.println();

      task();
    }
  } 
}

void task() {
    double currentLat = gps.location.lat();
    double currentLng = gps.location.lng();

    // Get Firebase coordinates
    double firebaseLat = Firebase.getFloat(firebaseData, "/coordinates/latitude");
    double firebaseLng = Firebase.getFloat(firebaseData, "/coordinates/longitude");

    // Compare coordinates
    double distance = TinyGPSPlus::distanceBetween(currentLat, currentLng, firebaseLat, firebaseLng);
    if (distance >= 5) {
      // Send email
      String message = "Current coordinates: " + String(currentLat, 6) + ", " + String(currentLng, 6) +
                       "\nFirebase coordinates: " + String(firebaseLat, 6) + ", " + String(firebaseLng, 6) +
                       "\nDifference: " + String(distance) + " meters";
     mailClient.setMessage(message.c_str(), message.length());
     mailClient.addRecipient(EMAIL_RECIPIENT);
     mailClient.send(smtpData);
     smtpData.empty();

        Serial.println("Over 5m away");
        Serial.println(message);
    }

    String maps = "https://www.google.com/maps/@" + String(currentLat, 6) + "," + String(currentLng, 6);
    
    // Update Firebase
    Firebase.setFloat(firebaseData, "/coordinates/latitude", currentLat);
    Firebase.setFloat(firebaseData, "/coordinates/longitude", currentLng);
    Firebase.setString(firebaseData, "/coordinates/maps", maps);
}