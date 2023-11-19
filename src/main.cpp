#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <FirebaseESP32.h>
#include <ESP_Mail_Client.h>

#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// WiFi credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Email credentials and settings
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL ""
#define AUTHOR_PASSWORD ""
#define RECIPIENT_EMAIL ""

SMTPSession smtp;

Session_Config config;

void smtpCallback(SMTP_Status status);

#include "HeapStat.h"
HeapStat heapInfo;

// RTC, GPS
RTC_DS1307 rtc;
TinyGPSPlus gps;
HardwareSerial SerialGPS(1);
#define DISTANCE 1

// Firebase credentials
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""
#define FIREBASE_USER ""
#define FIREBASE_PASSOWRD ""
#define FIREBASE_DATABASE_SECRET ""
#define FIREBASE_ROOT_PATH "/"
FirebaseData firebaseData;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;

// GPS pins
#define RX2 16
#define TX2 17

void firebaseSignIn(const char *email, const char *password) {
    firebaseAuth.user.email = email;
    firebaseAuth.user.password = password;

    /* Reset stored authen and config */
    Firebase.reset(&firebaseConfig);

    /* Initialize the library with the Firebase authen and config */
    Firebase.begin(&firebaseConfig, &firebaseAuth);

    // Setup user rules
    String var = "$userId";
    String val = "($userId === auth.uid && auth.token.premium_account === true && auth.token.admin === true)";
    Firebase.setReadWriteRules(firebaseData, "/", var, val, val, FIREBASE_DATABASE_SECRET);
}

void firebaseConnectionSetup(const char *apiKey, const char *host) {
    firebaseConfig.api_key = apiKey;
    firebaseConfig.database_url = host;
    firebaseConfig.token_status_callback = tokenStatusCallback;

    Firebase.reconnectNetwork(true);
}

void mailSetup(const char *host, const int port, const char *email, const char *password) {
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = AUTHOR_EMAIL;
    config.login.password = AUTHOR_PASSWORD;
    config.login.user_domain = F("127.0.0.1");

    config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
    config.time.gmt_offset = 2;
    config.time.day_light_offset = 0;
}

SMTP_Message prepareSmtpMessage(String currentLat, String currentLng, String firebaseLat, String firebaseLng, String distance, String mapsLink, String mapsDirections) {
    String msg = "Current coordinates: " + currentLat + ", " + currentLng + "\n" + "Firebase coordinates: " + firebaseLat + ", " + firebaseLng + "\n" + "Difference: " + distance + "meters\nMaps: " + mapsLink + "\nDirections: " + mapsDirections;
    SMTP_Message message;

    message.sender.name = F("ESP Vehicle");
    message.sender.email = AUTHOR_EMAIL;
    message.subject = F("Change in coordinates");
    message.addRecipient(F("user"), RECIPIENT_EMAIL);
    message.text.content = msg;
    message.text.charSet = F("us-ascii");

    return message;
}

void setup() {
    Serial.begin(115200);
    SerialGPS.begin(9600, SERIAL_8N1, RX2, TX2);
    Serial.println();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Connecting to Wi-Fi...");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("Finished connecting to Wi-Fi.");

    Serial.println("Connecting to RTC...");
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1) delay(10);
    }
    Serial.println("Finished connecting to RTC.");

    if (!rtc.isrunning()) {
        Serial.println("RTC is NOT running, let's set the time!");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
 
    // Initialize Firebase
    Serial.println("Connecting to Firebase...");
    firebaseConnectionSetup(FIREBASE_AUTH, FIREBASE_HOST);
    firebaseData.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);
    firebaseSignIn(FIREBASE_USER, FIREBASE_PASSOWRD);
    Serial.println("Finished Firebase initialization.");

    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    // Initialize email client
    Serial.println("Setting up mail client...");
    MailClient.networkReconnect(true);
    smtp.debug(1);
    smtp.callback(smtpCallback);
    mailSetup(SMTP_HOST, SMTP_PORT, AUTHOR_EMAIL, AUTHOR_PASSWORD);
    Serial.println("Finished mail client setup.");

    if (Firebase.ready()) {
        Serial.printf("Using path: %s\n", FIREBASE_ROOT_PATH);

        if (!Firebase.pathExisted(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/latitude")) {
            Serial.printf("Set latitude... %s\n", Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/latitude", "0") ? "ok" : firebaseData.errorReason().c_str());
        }
        if (!Firebase.pathExisted(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/longitude")) {
            Serial.printf("Set longitude... %s\n", Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/longitude", "0") ? "ok" : firebaseData.errorReason().c_str());
        }
        if (!Firebase.pathExisted(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/maps")) {
            Serial.printf("Set maps... %s\n", Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/maps", "placeholder") ? "ok" : firebaseData.errorReason().c_str());
        }
        if (!Firebase.pathExisted(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/directions")) {
            Serial.printf("Set maps directions... %s\n", Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/directions", "placeholder") ? "ok" : firebaseData.errorReason().c_str());
        }
    }
}

void printGpsData() {
    Serial.print(F("LOCATION   Fix Age="));
    Serial.print(gps.location.age());
    Serial.print(F("ms Raw Lat="));
    Serial.print(gps.location.rawLat().negative ? "-" : "+");
    Serial.print(gps.location.rawLat().deg);
    Serial.print("[+");
    Serial.print(gps.location.rawLat().billionths);
    Serial.print(F(" billionths],  Raw Long="));
    Serial.print(gps.location.rawLng().negative ? "-" : "+");
    Serial.print(gps.location.rawLng().deg);
    Serial.print("[+");
    Serial.print(gps.location.rawLng().billionths);
    Serial.print(F(" billionths],  Lat="));
    Serial.print(gps.location.lat(), 9);
    Serial.print(F(" Long="));
    Serial.println(gps.location.lng(), 9);
    Serial.printf("Satelites %s\n", String(gps.satellites.value()));
}

void task() {
    double currentLat = gps.location.lat();
    double currentLng = gps.location.lng();
    printGpsData();
    
    // Get Firebase coordinates
    Firebase.getString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/latitude");
    String firebaseLat = firebaseData.to<const char *>();
    Firebase.getString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/longitude");
    String firebaseLng = firebaseData.to<const char *>();
    String mapsLink = "https://www.google.com/maps/search/?api=1&query=" + String(currentLat, 9) + "," + String(currentLng, 9);
    String mapsDirections = "https://www.google.com/maps/dir/?api=1&destination=" + String(currentLat, 9) + "," + String(currentLng, 9);

    // Compare coordinates
    double distance = TinyGPSPlus::distanceBetween(currentLat, currentLng, firebaseLat.toDouble(), firebaseLng.toDouble());
    if (distance >= DISTANCE) {
        Serial.printf("Over %dm away", DISTANCE);

        // Prepare message
        SMTP_Message message = prepareSmtpMessage(String(currentLat, 9), String(currentLng, 9), firebaseLat, firebaseLng, String(distance), mapsLink, mapsDirections);

        Serial.println();
        Serial.println("Sending Email...");

        if (!smtp.isLoggedIn()) {
            /* Set the TCP response read timeout in seconds */
            smtp.setTCPTimeout(10);

            if (!smtp.connect(&config)) {
                MailClient.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
                goto exit;
            }

            if (!smtp.isLoggedIn()) {
                Serial.println("Error, Not yet logged in.");
                goto exit;
            } else {
                if (smtp.isAuthenticated())
                    Serial.println("Successfully logged in.");
                else
                    Serial.println("Connected with no Auth.");
            }
        }

        if (!MailClient.sendMail(&smtp, &message, false))
            MailClient.printf("Error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());

    exit:

        heapInfo.collect();
        heapInfo.print();
    }

    // Update Firebase
    Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/latitude", String(currentLat, 9));
    Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/longitude", String(currentLng, 9));
    Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/maps", mapsLink);
    Firebase.setString(firebaseData, String(FIREBASE_ROOT_PATH) + "/coordinates/directions", mapsDirections);
}

static uint32_t lastCheckForTask = uint32_t(0);

void loop() {
    DateTime now = rtc.now();
    bool newData = false;

    // Check if 30 seconds have passed
    if (now.unixtime() - lastCheckForTask >= uint32_t(60 * 0.5)) {
        if (!Firebase.ready()) {
            Serial.println("Firebase was not ready. Will reauthenticate.");
        }

        lastCheckForTask = rtc.now().unixtime();
        Serial.printf("\n30sec have passed. Executing task.\n");

        while (SerialGPS.available()) {
            char c = SerialGPS.read();

            if (gps.encode(c)) // Did a new valid sentence come in?
                newData = true;
        }
        
        if (newData) {
            task();
        }
    } 
}


/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status) {

    Serial.println(status.info());

    if (status.success()) {

        Serial.println("----------------");
        MailClient.printf("Message sent success: %d\n", status.completedCount());
        MailClient.printf("Message sent failed: %d\n", status.failedCount());
        Serial.println("----------------\n");

        for (size_t i = 0; i < smtp.sendingResult.size(); i++)
        {
            SMTP_Result result = smtp.sendingResult.getItem(i);

            MailClient.printf("Message No: %d\n", i + 1);
            MailClient.printf("Status: %s\n", result.completed ? "success" : "failed");
            MailClient.printf("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
            MailClient.printf("Recipient: %s\n", result.recipients.c_str());
            MailClient.printf("Subject: %s\n", result.subject.c_str());
        }
        Serial.println("----------------\n");

        smtp.sendingResult.clear();
    }
}