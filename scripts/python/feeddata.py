#!/usr/bin/env python3

import configparser
import time

from random import randrange

import pyrebase

DETECTOR_THRESHOLD = 15
DETECTED_THRESHOLD = 9

AX_THRESHOLD = 2000,
AY_THRESHOLD = 1000,
AZ_THRESHOLD = 17000


def get_value(n, m):
    return randrange(n, m, 1)


def get_detected(n, m):
    def detected():
        nonlocal n, m
        if n > m:
            n = get_value(0, 2)
        n += get_value(0, 2)
        return n-1
    return detected


def get_detector(n, m):
    d = 0

    def detector():
        nonlocal n, m, d
        if n > m:
            n = 1
            d = 0
        n += 1
        d += get_value(0, 2)

        return [n-1, d]
    return detector


def feed(db, user, detector_val, detected_val):
    ax = get_value(0, 10001)
    ay = get_value(0, 10001)
    az = get_value(0, 10001)

    data = {"accel": {
                "ax": ax,
                "ay": ay,
                "az": az
            },
            "threshold": {
                "ax": AX_THRESHOLD,
                "ay": AY_THRESHOLD,
                "az": AZ_THRESHOLD
            },
            "detector": {
                "detector": detector_val,
                "detected": detected_val,
                "detector_threshold": DETECTOR_THRESHOLD,
                "detected_threshold": DETECTED_THRESHOLD
            },
            "state": 0,
            "timestamp": {
                ".sv": "timestamp"
            }
    }

    print("Sending {}".format(data))
    db.child("dryer").push(data, user['idToken'])
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
    det = get_detector(1, 15)
    try:
        while True:
            d = det()
            feed(db, user, d[0], d[1])
            count += 1
            time.sleep(5)
    except KeyboardInterrupt:
        print("\nAnd we are done. Data sets sent: {}".format(count))

if __name__ == '__main__':
    main()
