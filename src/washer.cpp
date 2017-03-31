/*
Author: Nolan Gilley
License: CC-BY-SA, https://creativecommons.org/licenses/by-sa/2.0/
Date: 7/30/2016
File: laundry_esp8266.ino
This sketch is for a NodeMCU wired up with 2 accelerometers and
2 reed swithches.  An MPU6050 and a reed switch are attached to
both the wash and dryer.  This sketch will send mqtt messages to
the defined topics which describe the state of the washer and dryer
and the details of the accelerometer data.

1) Update the WIFI ssid and password below
2) Update the thresholds for washer and dryer movement

The thresholds are also configurable over mqtt!

The ESP8266 will publish mqtt commands to:
sensor/dryer/(EMPTY,RUNNING,COMPLETE)
sensor/washer/(EMPTY,RUNNING,COMPLETE)
sensor/dryer/detail: dryer %d, %d, %d, %d, %d", dryer_door_status, dryer_state, dryer_ax_range, dryer_ay_range, dryer_az_range
sensor/washer/detail: dryer %d, %d, %d, %d, %d", washer_door_status, washer_state, washer_ax_range, washer_ay_range, washer_az_range

And it SUBSCRIBES to:

sensor/dryer/set/ax
Ax value for dryer (int)
sensor/dryer/set/ay
Ay value for dryer (int)
sensor/dryer/set/az
Az value for dryer (int)
sensor/washer/set/ay
Ay value for washer (int)
sensor/dryer/set/detected
# of dryer detections out of 15 cycles
sensor/washer/set/detected
# of washer detections of of 15
sensor/dryer/set/detail
0 or 1 (off/on)
sensor/washer/set/detail
0 or 1

TODO:

1. Research alerting service that can be invoked directly from device
2. Fix docs
3. Send sufficient data to firebase
4. Fix MQTT subscriptions
5. Fix Firebase client

*/

// MPU6050 Includes
#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include "Statistic.h"

#include "secrets.h"

// ESP8266 Includes
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// PlatformIO needed these included explicitly
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>

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

#define DEVICE_NAME "washer"

Statistic accelStats;
Statistic detectorStats;

const char* firebaseRootName = DEVICE_NAME;

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTT Auth and Topic vars
const char* mqtt_server = MQTT_SERVER;
const char* mqtt_username = MQTT_USERNAME;
const char* mqtt_password = MQTT_PASSWORD;

char mqtt_topic_accel_avg[30];

// const char* mqtt_topic_accel = "sensor/dryer/accel";

/*
const char* mqtt_topic_ax = "sensor/dryer/ax";
const char* mqtt_topic_ay = "sensor/dryer/ay";
const char* mqtt_topic_az = "sensor/dryer/az";
*/

// const char* mqtt_topic_app_detail = "sensor/dryer/detail";
char mqtt_topic_app_status[30];

// const char* mqtt_topic_app_status = "sensor/dryer/status";

char mqtt_topic_set_subscribe[30];
// const char* mqtt_topic_set_subscribe = "sensor/dryer/set/+";

char mqtt_topic_set_accel_avg[30];
// const char* mqtt_topic_set_accel = "sensor/dryer/accel";

/*
const char* mqtt_topic_set_ax = "sensor/dryer/set/ax";
const char* mqtt_topic_set_ay = "sensor/dryer/set/ay";
const char* mqtt_topic_set_az = "sensor/dryer/set/az";
*/

char mqtt_topic_set_detected_threshold[50];
// const char* mqtt_topic_set_detected_threshold = "sensor/dryer/set/detected_threshold";

char mqtt_topic_set_detector_threshold[50];
// const char* mqtt_topic_set_detected_threshold = "sensor/dryer/set/detector_threshold";

char mqtt_topic_set_mqtt_publish_data[50];
//const char* mqtt_topic_set_mqtt_publish_data = "sensor/dryer/set/mqtt_publish_data";

char mqtt_topic_set_firebase_send_data[50];
// const char* mqtt_topic_set_firebase_send_data = "sensor/dryer/set/firebase_send_data";

WiFiClient espClient;
PubSubClient client(espClient);

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
int app_reading = 0; // reading = 1 mean no movement, 0=movement
// int app_reading_previous = 0;
int app_state = STOPPED;  // 1 = running, 0 = stopped
int last_app_state = STOPPED;

unsigned long sample_time = 0;   //millis of last reading
unsigned long reconnect_time = 0; //millis of last reconnect attempt

int app_detector_count = 0;   //number of captures
int app_detected_count = 0;   //number of readings showing movement

char appString[100];   // output data to serial buffer

// stat variables
int app_accel_avg = 0;
int detector_std = 0;
int detector_avg = 0;

// CONFIGURABLE THRESHOLDS

// app_accel_avg over this value == movement for the sample
int app_accel_threshold = 6100;

// num of positive samples required to consider interval as having movement
int app_detected_threshold = 12;

// num of samples that comprise an interval
int app_detector_threshold = 30;

// max standard deviation to determine movement
int detector_std_threshold_1 = 120;

// max standard deviation that, along with deterctor_avg, determine movement
int detector_std_threshold_2 = 100;

// interval sample average over limit along with STD determine movement
int detector_avg_threshold = 6200;

//int app_ax_threshold = 2000;
//int app_ay_threshold = 1000;
//int app_az_threshold = 17000;

//int app_ax_threshold = 17500;
//int app_ay_threshold = 2000;
//int app_az_threshold = 3300;

//int washer_ax_threshold = 17500;
//int washer_ay_threshold = 18000;
//int washer_az_threshold = 3300;

int reconnect_delay = 60;   // for MQTT, sec

// Reporting
int app_mqtt_publish_data = 1;
int app_firebase_send_data = 1;

// Firebase config
DynamicJsonBuffer jsonBuffer;
JsonObject& firebaseObject = jsonBuffer.createObject();
JsonObject& accel = firebaseObject.createNestedObject("accel");
//JsonObject& threshold = firebaseObject.createNestedObject("threshold");
JsonObject& detector = firebaseObject.createNestedObject("detector");
JsonObject& tempTime = firebaseObject.createNestedObject("timestamp");

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
void callback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  /*if (strcmp(topic, mqtt_topic_set_ax) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_ax_threshold = i;
    Serial.print("appliance ax set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_ay) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_ay_threshold = i;
    Serial.print("appliance ay set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_az) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_az_threshold = i;
    Serial.print("appliance az set to ");
    Serial.println(i);
  } */
  if (strcmp(topic, mqtt_topic_set_accel_avg) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_accel_threshold = i;
    Serial.print("appliance accel threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_detected_threshold) == 0) {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_detected_threshold = i;
    Serial.print("appliance detected threshold set to ");
    Serial.println(i);
  }
  else if (strcmp(topic, mqtt_topic_set_detector_threshold) == 0)
  {
    payload[length] = '\0';
    String s = String((char*)payload);
    int i= s.toInt();
    app_detector_threshold = i;
    Serial.print("appliance detector threshold set to ");
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
}

void reconnect()
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
}

void update_via_mqtt()
{
  if (app_state == RUNNING) {
    client.publish(mqtt_topic_app_status, "Running", true);
  }
  else {
    client.publish(mqtt_topic_app_status, "Stopped", true);
  }
  Serial.println("mqtt published!");
}

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
  sprintf(mqtt_topic_accel_avg, "sensor/%s/accel_avg", DEVICE_NAME);
  sprintf(mqtt_topic_app_status, "sensor/%s/status", DEVICE_NAME);

  sprintf(mqtt_topic_set_subscribe, "sensor/%s/set/+", DEVICE_NAME);
  sprintf(mqtt_topic_set_accel_avg, "sensor/%s/set/accel_avg", DEVICE_NAME);
  sprintf(mqtt_topic_set_detected_threshold, "sensor/%s/set/detected_threshold", DEVICE_NAME);
  sprintf(mqtt_topic_set_detector_threshold, "sensor/%s/set/detector_threshold", DEVICE_NAME);
  sprintf(mqtt_topic_set_mqtt_publish_data, "sensor/%s/set/mqtt_publish_data", DEVICE_NAME);
  sprintf(mqtt_topic_set_firebase_send_data, "sensor/%s/set/firebase_send_data", DEVICE_NAME);

  Serial.begin(115200); // setup serial

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  Wire.begin();
  MPU_APPLIANCE.initialize();

  reconnect();
  reconnect_time = millis();

  update_via_mqtt();

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
  if (reconnect_time > millis())
  {
    reconnect_time = millis();
  }

  // attempt MQTT reconnect every [reconnect_delay] seconds
  if (!client.connected() && ((millis() - reconnect_time) > (reconnect_delay*1000)))
  {
    reconnect();
    reconnect_time = millis();
  }

  client.loop();

  // get data from accelerometer and update min/max
  MPU_APPLIANCE.getMotion6(&app_ax, &app_ay, &app_az, &app_gx, &app_gy, &app_gz);
  trackMinMax(app_ax, &app_ax_min, &app_ax_max);
  trackMinMax(app_ay, &app_ay_min, &app_ay_max);
  trackMinMax(app_az, &app_az_min, &app_az_max);

  // samples every 5 sec
  if ((millis() - sample_time) > 5000)
  {
    sample_time = millis();    //reset sample_time to wait for next Xms

    // store expression result before calling abs() since it is a macro and can result in race condition wierdness (i.e. neg result)
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
    // sprintf(appString, "%d, %d/%d, %d/%d, %d/%d", app_state, app_ax_range, app_ax_threshold, app_ay_range, app_ay_threshold, app_az_range, app_az_threshold);
    sprintf(appString, "S: %d, A: %d/%d, DR: %d/%d, DD: %d/%d, STD: %d, AVG: %d", app_state, app_accel_avg, app_accel_threshold,
                                                     app_detector_count, app_detector_threshold, app_detected_count,
                                                     app_detected_threshold, detector_std, detector_avg);
    Serial.println(appString);

    // publish data via MQTT
    if (app_mqtt_publish_data == 1)
    {
      //client.publish(mqtt_topic_app_detail, appString);

      client.publish(mqtt_topic_accel_avg, String(app_accel_avg).c_str());
      /*
      client.publish(mqtt_topic_ax, String(app_ax_range).c_str());
      client.publish(mqtt_topic_ay, String(app_ay_range).c_str());
      client.publish(mqtt_topic_az, String(app_az_range).c_str());
      */
    }

    // send data to Firebase
    if (app_firebase_send_data == 1)
    {

      accel["avg"]       = app_accel_avg;
      accel["threshold"] = app_accel_threshold;
      /*
      accel["ax"] = app_ax_range;
      accel["ay"] = app_ay_range;
      accel["az"] = app_az_range;

      threshold["ax"] = app_ax_threshold;
      threshold["ay"] = app_ay_threshold;
      threshold["az"] = app_az_threshold;
      */

      detector["detected"]           = app_detected_count;
      detector["detector"]           = app_detector_count;
      detector["detected_threshold"] = app_detected_threshold;
      detector["detector_threshold"] = app_detector_threshold;

      firebaseObject["state"]        = app_state;
      tempTime[".sv"]                = "timestamp";

      Firebase.push(firebaseRootName, firebaseObject);
      if (Firebase.failed())
      {
          Serial.print("Error sending to Firebase:");
          Serial.println(Firebase.error());
      }
    }

    // if ((app_ax_range > app_ax_threshold) and (app_ay_range > app_ay_threshold) and (app_az_range > app_az_threshold))
    if (app_accel_avg > app_accel_threshold)
    {
      app_reading = MOVEMENT_DETECTED;
    }

    app_detector_count++;     // count samples

    if (app_reading == MOVEMENT_DETECTED)
    {
      app_detected_count++;   // count samples with movement
    }

    app_reading = MOVEMENT_NOT_DETECTED;        // reset
  } //end sampling every 5 seconds

  // all samples for this interval have been collected, determine movement for interval
  if (app_detector_count == app_detector_threshold)
  {
    detector_std = (int) detectorStats.pop_stdev();
    detector_avg = (int) detectorStats.average();

    // Movement is determined by:
    // 1. enough positive samples and STD threshold 1 reached
    // or
    // 2. enough positive samples and STD threshold 2 reached and sample avg below threshold
    if ((app_detected_count >= app_detected_threshold) &&
       ((detector_std >= detector_std_threshold_1) || ((detector_std > detector_std_threshold_2) && (detector_avg < detector_avg_threshold))))
    {
      app_state = RUNNING;
    }
    else
    {
      if (app_state == RUNNING)
      {
        app_state = STOPPED;
      }
    }

    // reset
    app_detector_count = 0;
    app_detected_count = 0;
    detectorStats.clear();
  }

  // alert if this interval's state differs from the last
  if (last_app_state != app_state)
  {
    update_via_mqtt();
  }
}
