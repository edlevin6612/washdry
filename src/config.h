// app_accel_avg over this value == movement for the sample
#define APP_ACCEL_THRESHOLD 6100

// num of positive samples required to consider interval as having movement
#define APP_DETECTED_THRESHOLD 12

// num of samples that comprise an interval
#define APP_DETECTOR_THRESHOLD 30

// max standard deviation to determine movement
#define DETECTOR_STD_THRESHOLD_1 120

// max standard deviation that, along with deterctor_avg, determine movement
#define DETECTOR_STD_THRESHOLD_2 100

// interval sample average over limit along with STD determine movement
#define DETECTOR_AVG_THRESHOLD 6200
