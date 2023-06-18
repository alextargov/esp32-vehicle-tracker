#include <Wire.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <FirebaseESP32.h>
#include <ESP_Mail_Client.h>

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

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
SMTPSession smtp;

// GPS pins
#define RX2 16
#define TX2 17


// SMTPData smtpData;
ESP_Mail_Client mailClient;
ESP_Mail_Session session;

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

  smtp.callback(smtpCallback);

  /* Set the session config */
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = EMAIL_SENDER;
  session.login.password = EMAIL_SENDER_PASSWORD;
  session.login.user_domain = "";
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
      if (!sendEmail(currentLat, currentLng, firebaseLat, firebaseLng, distance)) {
        Serial.println("Faield sending Email");
      }
    }

    // Update Firebase
    Firebase.setFloat(firebaseData, "/coordinates/latitude", currentLat);
    Firebase.setFloat(firebaseData, "/coordinates/longitude", currentLng);
    String maps = "https://www.google.com/maps/@" + String(currentLat, 7) + "," + String(currentLng, 7);
    Firebase.setString(firebaseData, "/coordinates/maps", maps);
}

bool sendEmail(double currentLat, double currentLng, double firebaseLat, double firebaseLng, double distance) {
  SMTP_Message message;
  String messageStr = "Current coordinates: " + String(currentLat, 7) + ", " + String(currentLng, 7) +
                   "\nFirebase coordinates: " + String(firebaseLat, 7) + ", " + String(firebaseLng, 7) +
                   "\nDifference: " + String(distance) + " meters";
  /* Set the message headers */
  message.sender.name = "ESP";
  message.sender.email = EMAIL_SENDER;
  message.subject = "ESP Test Email";
  message.addRecipient("Sara", EMAIL_RECIPIENT);
  /*Send HTML message*/
  String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Hello World!</h1><p>- Sent from ESP board</p></div>";
  message.html.content = htmlMsg.c_str();
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  Serial.println("Over 5m away");
  Serial.println(messageStr);
  if (!smtp.connect(&session)) {
      return false;
  }
  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Error sending Email, " + smtp.errorReason());
    return false;
  }

  return true;
}

void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");
  }
}