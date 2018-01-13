// name of the appliance being monitored (e.g. washer or dryer)
// this value serves as an identifier for Firebase and MQTT topic names
#define DEVICE_NAME "washer"

// Detector Settings

// app_accel_avg over this value == movement for the sample
#define APP_ACCEL_THRESHOLD 6300

// num of positive samples required to consider interval as having movement
#define APP_DETECTED_THRESHOLD 3

// num of samples that comprise an interval
#define APP_DETECTOR_THRESHOLD 6

// max standard deviation to determine movement
#define DETECTOR_STD_THRESHOLD_1 100
// #define DETECTOR_STD_THRESHOLD_1 270

// max standard deviation that, along with deterctor_avg, determine movement
// #define DETECTOR_STD_THRESHOLD_2 240

// interval sample average over limit along with STD determine movement
#define DETECTOR_AVG_THRESHOLD 6600

// Notification Settings

// SMTP hostname
#define SMTP_HOST "smtp.gmail.com"

// SMTP port
#define SMTP_PORT_NUM 465
