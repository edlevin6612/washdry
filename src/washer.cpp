/*

Author: Ed Levin
License: CC-BY-SA, https://creativecommons.org/licenses/by-sa/2.0/
Date: 3/31/2017

Based on the original sketch by:
Author: Nolan Gilley
https://github.com/nkgilley/nodemcu-laundry

This sketch is for a NodeMCU wired up with an accelerometer.
The combo is connected to either a washer or a dryer.
This sketch will send mqtt messages to the defined topics as well as
Firebase (optional) describing the state of the washer or dryer and
the details of the accelerometer data.

To get set up:

1) Rename secrets-template.h to secrets.h and fill in WIFI, MQTT broker, Firebase credentials
2) Update the thresholds in config.h (might need to collect some baseline data first)

The thresholds are also configurable over mqtt via these topics:

sensor/<DEVILE_NAME>/set/accel_threshold
sensor/<DEVILE_NAME>/set/detected_threshold
sensor/<DEVILE_NAME>/set/detector_threshold
sensor/<DEVILE_NAME>/set/detector_std_threshold_1
sensor/<DEVILE_NAME>/set/detector_std_threshold_2
sensor/<DEVILE_NAME>/set/detector_avg_threshold

The ESP8266 will publish mqtt commands to:

sensor/<DEVICE_NAME>/status (Running/Stopped)
sensor/<DEVICE_NAME/accel_avg

And it SUBSCRIBES to:

sensor/<DEVICE_NAME>/set/accel_threshold
(int)
sensor/<DEVICE_NAME>/set/detected_threshold
(int)
sensor/<DEVICE_NAME>/set/detector_threshold
(int)
sensor/<DEVICE_NAME>/set/detector_std_threshold_1
(int)
sensor/<DEVICE_NAME>/set/detector_std_threshold_2
(int)
sensor/<DEVICE_NAME>/set/detector_avg_threshold
(int)
sensor/<DEVICE_NAME>/set/mqtt_publish_data
0 or 1
sensor/<DEVICE_NAME>/set/firebase_send_data
0 or 1

*/

// MPU6050 Includes
#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include "Statistic.h"
#include "Gsender.h"

#include "secrets.h"
#include "config.h"

// ESP8266 Includes
#include <ESP8266WiFi.h>
//#include <PubSubClient.h>

// PlatformIO needed these included explicitly
#include <ESP8266HTTPClient.h>
//#include <ESP8266WebServer.h>

// Firebase client
#include <Firebase.h>
#include <FirebaseArduino.h>
#include <FirebaseCloudMessaging.h>
#include <FirebaseError.h>
#include <FirebaseHttpClient.h>
#include <FirebaseObject.h>

// DEFINE NODEMCU PINS
#define D0 16
#define D1 5 // I2C Bus SCL (clock)
#define D2 4 // I2C Bus SDA (data)
#define D3 0
#define D4 2 // Same as "LED_BUILTIN", but inverted logic
#define D5 14 // SPI Bus SCK (clock)
#define D6 12 // SPI Bus MISO
#define D7 13 // SPI Bus MOSI
#define D8 15 // SPI Bus SS (CS)
#define D9 3 // RX0 (Serial console)
#define D10 1 // TX0 (Serial console)

// DEFINE STATES
#define STOPPED 0 //washer/dryer stopped
#define RUNNING 1 //washer/dryer running
#define MOVEMENT_DETECTED 0
#define MOVEMENT_NOT_DETECTED 1

Statistic accelStats;
Statistic detectorStats;

const char* firebaseRootName = DEVICE_NAME;

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTT Auth and Topic vars
/*
const char* mqtt_server = MQTT_SERVER;
const char* mqtt_username = MQTT_USERNAME;
const char* mqtt_password = MQTT_PASSWORD;

char mqtt_topic_accel_avg[30];

char mqtt_topic_app_status[30];
char mqtt_topic_set_subscribe[30];
char mqtt_topic_set_accel_threshold[30];

char mqtt_topic_set_detected_threshold[50];
char mqtt_topic_set_detector_threshold[50];
char mqtt_topic_set_detector_std_threshold_1[50];
// char mqtt_topic_set_detector_std_threshold_2[50];
char mqtt_topic_set_detector_avg_threshold[50];

char mqtt_topic_set_mqtt_publish_data[50];
char mqtt_topic_set_firebase_send_data[50];

WiFiClient espClient;
PubSubClient client(espClient); */

// MPU-6050 Accelerometer ------------------------------------------------------
// 0x68 default I2C address
// 0x69 (3.3V --> AD0 pin)
MPU6050 MPU_APPLIANCE(0x68);

int16_t app_ax, app_ay, app_az;
int16_t app_gx, app_gy, app_gz;
int16_t app_ax_min = 0, app_ax_max = 0, app_ax_range, app_ax_range_raw;
int16_t app_ay_min = 0, app_ay_max = 0, app_ay_range, app_ay_range_raw;
int16_t app_az_min = 0, app_az_max = 0, app_az_range, app_az_range_raw;
// end MPU-6050-----------------------------------------------------------------

// accelerometer sensor ===============================================
// int app_reading = 1; // reading = 1 mean no movement, 0=movement
// int app_reading_previous = 0;
int app_state = STOPPED;  // 1 = running, 0 = stopped
int last_app_state = STOPPED;

unsigned long sample_time = 0;   //millis of last reading
unsigned long reconnect_time = 0; //millis of last reconnect attempt

int app_detector_count = 0;   //number of captures
int app_detected_count = 0;   //number of readings showing movement

char sampleStats[100];   // output sample stats to serial buffer
char intervalStats[100];  // output interval stats to serial buffer

// stat variables
int app_accel_avg = 0;
int detector_std = 0;
int detector_avg = 0;

// THRESHOLDS
int app_accel_threshold = APP_ACCEL_THRESHOLD;
int app_detected_threshold = APP_DETECTED_THRESHOLD;
int app_detector_threshold = APP_DETECTOR_THRESHOLD;
int detector_std_threshold_1 = DETECTOR_STD_THRESHOLD_1;
// int detector_std_threshold_2 = DETECTOR_STD_THRESHOLD_2;
int detector_avg_threshold = DETECTOR_AVG_THRESHOLD;

//int reconnect_delay = 60;   // for MQTT, sec

// Reporting
//int app_mqtt_publish_data = 1;
int app_firebase_send_data = 1;

// Firebase config
DynamicJsonBuffer jsonBuffer;
JsonObject& firebaseObject = jsonBuffer.createObject();
JsonObject& accel = firebaseObject.createNestedObject("accel");
JsonObject& detector = firebaseObject.createNestedObject("detector");
JsonObject& detected = firebaseObject.createNestedObject("detected");
JsonObject& tempTime = firebaseObject.createNestedObject("timestamp");

// Gmail sender
Gsender *gsender = Gsender::Instance();
String to_address = TO_ADDRESS;

void send_notification(const String &message)
{
  String device = String(DEVICE_NAME);
  device.toUpperCase();
  String subject = device + " STATUS UPDATE";

  Serial.println("Sending notification...");
  if(gsender->Subject(subject)->Send(to_address, message)) {
      Serial.println("Notification sent");
  } else {
      Serial.print("Error sending notification: ");
      Serial.println(gsender->getError());
  }
}

void setup_wifi()
{
  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// process incoming MQTT messages
/*void callback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, mqtt_topic_set_accel_threshold) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_accel_threshold = i;
    Serial.print("accel threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_detected_threshold) == 0) {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_detected_threshold = i;
    Serial.print("detected threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_detector_threshold) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_detector_threshold = i;
    Serial.print("detector threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_detector_std_threshold_1) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    detector_std_threshold_1 = i;
    Serial.print("detector STD threshold 1 set to ");
    Serial.println(i);
  } */
/* else if (strcmp(topic, mqtt_topic_set_detector_std_threshold_2) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    detector_std_threshold_2 = i;
    Serial.print("detector STD threshold 2 set to ");
    Serial.println(i);
  } */
  /*else if (strcmp(topic, mqtt_topic_set_detector_avg_threshold) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    detector_avg_threshold = i;
    Serial.print("detector AVG threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_mqtt_publish_data) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_mqtt_publish_data = i;
    Serial.print("appliance MQTT publish data set to ");
    Serial.println(i);
  }
   else if (strcmp(topic, mqtt_topic_set_firebase_send_data) == 0)
   {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_firebase_send_data = i;
    Serial.print("appliance Firebase send data set to ");
    Serial.println(i);
  }
} */

/*void reconnect()
{
  // Attempt to connect 3 times and continue
  int attempt = 1;
  while (!client.connected() && attempt <= 3)
  {
    Serial.print("Attempting MQTT connection ");
    Serial.println(attempt);
    attempt++;
    // Attempt to connect
    if (client.connect("ESP8266Client", mqtt_username, mqtt_password))
    {
      Serial.println("MQTT connection established");
      client.subscribe(mqtt_topic_set_subscribe);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 3 seconds");
      // Wait 3 seconds before retrying
      delay(3000);
    }
    if (attempt > 3) {
      Serial.print("MQTT connection failed, will try again in ");
      Serial.print(reconnect_delay);
      Serial.println(" seconds");
    }
  }
}*/

/*void update_via_mqtt()
{
  if (app_state == RUNNING) {
    client.publish(mqtt_topic_app_status, "Running", true);
  }
  else {
    client.publish(mqtt_topic_app_status, "Stopped", true);
  }
  Serial.println("mqtt published!");
}*/

int16_t trackMinMax(int16_t current, int16_t *min, int16_t *max)
{
  if (current > *max)
  {
    *max = current;
  }
  else if (current < *min)
  {
    *min = current;
  }
}

void setup()
{
  // interpolate MQTT topic names
  /*
  sprintf(mqtt_topic_accel_avg, "sensor/%s/accel_avg", DEVICE_NAME);
  sprintf(mqtt_topic_app_status, "sensor/%s/status", DEVICE_NAME);

  sprintf(mqtt_topic_set_subscribe, "sensor/%s/set/+", DEVICE_NAME);

  sprintf(mqtt_topic_set_accel_threshold, "sensor/%s/set/accel_threshold", DEVICE_NAME);
  sprintf(mqtt_topic_set_detected_threshold, "sensor/%s/set/detected_threshold", DEVICE_NAME);
  sprintf(mqtt_topic_set_detector_threshold, "sensor/%s/set/detector_threshold", DEVICE_NAME);
  sprintf(mqtt_topic_set_detector_std_threshold_1, "sensor/%s/set/detector_std_threshold_1", DEVICE_NAME);
  // sprintf(mqtt_topic_set_detector_std_threshold_2, "sensor/%s/set/detector_std_threshold_2", DEVICE_NAME);
  sprintf(mqtt_topic_set_detector_avg_threshold, "sensor/%s/set/detector_avg_threshold", DEVICE_NAME);

  sprintf(mqtt_topic_set_mqtt_publish_data, "sensor/%s/set/mqtt_publish_data", DEVICE_NAME);
  sprintf(mqtt_topic_set_firebase_send_data, "sensor/%s/set/firebase_send_data", DEVICE_NAME); */

  Serial.begin(115200); // setup serial

  setup_wifi();

  // client.setServer(mqtt_server, 1883);
  // client.setCallback(callback);

  Wire.begin();
  MPU_APPLIANCE.initialize();

  // reconnect();
  // reconnect_time = millis();

  // update_via_mqtt();

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  accelStats.clear();
  detectorStats.clear();
}

void loop()
{
  last_app_state = app_state;

  // deal with millis rollover
  if (sample_time > millis())
  {
    sample_time = millis();
  }
  /*if (reconnect_time > millis())
  {
    reconnect_time = millis();
  }*/

  // attempt MQTT reconnect every [reconnect_delay] seconds
  /*if (!client.connected() && ((millis() - reconnect_time) > (reconnect_delay*1000)))
  {
    reconnect();
    reconnect_time = millis();
  }

  client.loop();*/

  // get data from accelerometer and update min/max
  MPU_APPLIANCE.getMotion6(&app_ax, &app_ay, &app_az, &app_gx, &app_gy, &app_gz);
  trackMinMax(app_ax, &app_ax_min, &app_ax_max);
  trackMinMax(app_ay, &app_ay_min, &app_ay_max);
  trackMinMax(app_az, &app_az_min, &app_az_max);

  // samples every 5 sec
  if ((millis() - sample_time) > 5000)
  {
    sample_time = millis();    //reset sample_time to wait for next Xms

    // store expression result before calling abs() since it is a macro and can result in race condition weirdness (i.e. neg result)
    app_ax_range_raw = app_ax_max - app_ax_min;
    app_ay_range_raw = app_ay_max - app_ay_min;
    app_az_range_raw = app_az_max - app_az_min;

    // calculate range for each accelerometer direction
    app_ax_range = abs(app_ax_range_raw);
    app_ay_range = abs(app_ay_range_raw);
    app_az_range = abs(app_az_range_raw);

    // reset range counters
    app_ax_min = 0, app_ax_max = 0;
    app_ay_min = 0, app_ay_max = 0;
    app_az_min = 0, app_az_max = 0;

    // calculate a mean of all direction values, this is our sample
    accelStats.add((float) app_ax_range);
    accelStats.add((float) app_ay_range);
    accelStats.add((float) app_az_range);
    app_accel_avg = (int) accelStats.average();

    detectorStats.add((float) app_accel_avg);

    accelStats.clear();

    // Output to serial monitor
    sprintf(sampleStats, "Sample Stats: (S: %d, A: %d/%d, DR: %d/%d, DD: %d/%d)", app_state, app_accel_avg, app_accel_threshold,
                                                     app_detector_count, app_detector_threshold, app_detected_count,
                                                     app_detected_threshold);
    Serial.println(sampleStats);

    // only send data on activity
    //if (app_detected_count >= app_detected_threshold) {

      // publish data via MQTT
      /*if (app_mqtt_publish_data == 1)
      {
        client.publish(mqtt_topic_accel_avg, String(app_accel_avg).c_str());
      }*/

      // send data to Firebase
      Serial.printf("heap: %u\n", ESP.getFreeHeap());
      if (app_firebase_send_data == 1)
      {
        accel["avg"]                   = app_accel_avg;
        accel["threshold"]             = app_accel_threshold;

        detector["count"]              = app_detector_count;
        detector["threshold"]          = app_detector_threshold;

        detected["count"]              = app_detected_count;
        detected["threshold"]          = app_detected_threshold;

        detected["std"]                = detector_std;
        detected["avg"]                = detector_avg;
        detected["std_threshold_1"]    = detector_std_threshold_1;
        // detected["std_threshold_2"]    = detector_std_threshold_2;
        detected["avg_threshold"]      = detector_avg_threshold;

        firebaseObject["state"]        = app_state;
        tempTime[".sv"]                = "timestamp";

        Firebase.push(firebaseRootName, firebaseObject);
        if (Firebase.failed())
        {
            Serial.print("Error sending to Firebase:");
            Serial.println(Firebase.error());
        }
      }
    //}

    if (app_accel_avg > app_accel_threshold)
    {
      // app_reading = MOVEMENT_DETECTED;
      app_detected_count++;
    }

    app_detector_count++;     // count samples

    /*if (app_reading == MOVEMENT_DETECTED)
    {
      app_detected_count++;   // count samples with movement
    }

    app_reading = MOVEMENT_NOT_DETECTED;        // reset*/
  } //end sampling every 5 seconds

  // all samples for this interval have been collected, determine movement for interval
  if (app_detector_count == app_detector_threshold)
  {
    detector_std = (int) detectorStats.pop_stdev();
    detector_avg = (int) detectorStats.average();

    // Output to serial monitor
    sprintf(intervalStats, "Interval Stats: (STD: %d, AVG: %d)", detector_std, detector_avg);
    Serial.println(intervalStats);

    // Movement is determined by:
    // 1. enough positive samples and STD threshold 1 reached
    // or
    // 2. enough positive samples and sample avg above threshold
    if ((app_detected_count >= app_detected_threshold) &&
       ((detector_std >= detector_std_threshold_1) || (detector_avg >= detector_avg_threshold)))
    {
      if (last_app_state == STOPPED)
      {
        app_state = RUNNING;
        send_notification("Cycle Started");
      }
    }
    else
    {
      if (last_app_state == RUNNING)
      {
        app_state = STOPPED;
        send_notification("Cycle Finished");
      }
    }

    // reset for next interval
    app_detector_count = 0;
    app_detected_count = 0;
    detectorStats.clear();
  }

  // alert if this interval's state differs from the last
  /*if (last_app_state != app_state)
  {
    update_via_mqtt();
  }*/
}
