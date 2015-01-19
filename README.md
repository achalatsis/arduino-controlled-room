This is a work in progress and several features are missing.

The goal is to have an Arduino control all the features of
a room. These features could included, but not limited to:
* switching lights
* control various IR controlled devices suchs as ceiling fans
and ACs
* use PIR and heat sensors
* use lighting as visual alarms

For current development status check the issues section.

---

Currently implemented features:
* web server for accessing arduino
* WOL for waking up computer when presence is detected
* PIR sensor for motion detection (lights)
* IR control of AC

Missing features:
* network time for using lights as alarm clock
