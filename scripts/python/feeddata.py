#!/usr/bin/env python3

import configparser
import statistics
import sys
import time

from random import randrange

import pyrebase

ACCEL_THRESHOLD = 6100

DETECTOR_THRESHOLD = 6
DETECTED_THRESHOLD = 3

DETECTOR_STD_THRESHOLD_1 = 120
DETECTOR_STD_THRESHOLD_2 = 100
DETECTOR_AVG_THRESHOLD = 6200

RANDOM_ACCEL_MIN = 5500
RANDOM_ACCEL_MAX = 6500

def get_value(n, m):
    return randrange(n, m, 1)

"""
def get_detected(n, m):
    def detected():
        nonlocal n, m
        if n > m:
            n = get_value(0, 2)
        n += get_value(0, 2)
        return n-1
    return detected
"""

def get_detector(n, m):
    # d = 0

    def detector():
        nonlocal n, m #, d
        if n > m:
            n = 1
            # d = 0
        n += 1
        # d += get_value(0, 2)

        return [n-1] # dß
    return detector


def feed(db, user, accel_avg, detector_val, detected_val, detector_std, detector_avg, state):
    data = {"accel": {
                "avg": accel_avg,
                "threshold": ACCEL_THRESHOLD
            },
            "detector": {
                "count": detector_val,
                "threshold": DETECTOR_THRESHOLD
            },
            "detected": {
                "count": detected_val,
                "threshold": DETECTED_THRESHOLD,
                "std": detector_std,
                "avg": detector_avg,
                "std_threshold_1": DETECTOR_STD_THRESHOLD_1,
                "std_threshold_2": DETECTOR_STD_THRESHOLD_2,
                "avg_threshold": DETECTOR_AVG_THRESHOLD
            },
            "state": state,
            "timestamp": {
                ".sv": "timestamp"
            }
    }

    print("Sending {}".format(data))
    db.child("washer").push(data, user['idToken'])
    #user = db.child("test").get(user['idToken'])
    #print(user.val())

def main():

    section = 'default'
    secrets = configparser.RawConfigParser()
    secrets.read('./secrets.ini')

    config = {
      "apiKey": secrets.get(section, 'firebase_api_key'),
      "authDomain": secrets.get(section, 'firebase_auth_domain'),
      "databaseURL": secrets.get(section, 'firebase_database_url'),
      "storageBucket": secrets.get(section, 'firebase_storage_bucket')
    }

    firebase = pyrebase.initialize_app(config)

    firebase_user = secrets.get(section, 'firebase_user')
    firebase_pass = secrets.get(section, 'firebase_pass')

    auth = firebase.auth()
    user = auth.sign_in_with_email_and_password(firebase_user, firebase_pass)

    db = firebase.database()

    count = 0
    detector_avg = 0
    detector_std = 0
    accel_vals = []
    detected_count = 0
    state = 0

    det = get_detector(1, DETECTOR_THRESHOLD)

    try:
        while True:
            d = det()
            detector_count = d[0]

            accel_avg = get_value(RANDOM_ACCEL_MIN, RANDOM_ACCEL_MAX)
            accel_vals.append(accel_avg)

            if accel_avg > ACCEL_THRESHOLD:
                detected_count += 1

            # end of interval reached
            if detector_count == DETECTOR_THRESHOLD:
                # calculate STD and AVG
                detector_avg = int(statistics.mean(accel_vals))
                detector_std = int(statistics.pstdev(accel_vals))

                if ((detected_count >= DETECTED_THRESHOLD) and ((detector_std > DETECTOR_STD_THRESHOLD_1) or ((detector_std > DETECTOR_STD_THRESHOLD_2) and (detector_avg < DETECTOR_AVG_THRESHOLD)))):
                    state = 1
                else:
                    state = 0

                accel_vals = []

            # send to Firebase
            feed(db, user, accel_avg, detector_count, detected_count, detector_std, detector_avg, state)

            if detector_count == DETECTOR_THRESHOLD:
                detected_count = 0

            count += 1
            time.sleep(5)
    except KeyboardInterrupt:
        print("\nAnd we are done. Data sets sent: {}".format(count))

if __name__ == '__main__':
    main()
