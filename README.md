# Frontier TrailCam
## Remotely accessible, Programmable, Arduino-based, Timelapse Camera Stations
<img width="982" alt="Screenshot 2025-05-02 at 12 46 38 AM" src="https://github.com/user-attachments/assets/25ab02a0-4f61-43b5-a421-9fa08e7b2a63" />
Frontier TrailCam is a simple, open-source timelapse camera system. It runs on the ESP32-CAM, a small, Arduino-compatible board equipped with a camera to take HD photos. Frontier TrailCam is compact and portable, giving it flexibility to be deployed in many applications, whether you're tracking changes in the invasive species at a park, watching flowers grow in your garden, or filming a sunset. With flexible firmware and a web interface for adjusting settings, it's a fun and practical open-source TrailCam for anyone needing a portable timelapse camera.

### Technical Overview
- Frontier TrailCam is built around the ESP32-CAM, an Arduino-compatible system-on-chip (SoC). The device features two physical controls on its side: a power switch and a mode button.
- When powered on, pressing the mode button during startup launches the TrailCam’s built-in web server. Through this interface, users can adjust camera settings such as photo resolution and capture interval, as well as view a live feed to help position the camera. If the button is not pressed at startup, the camera will operate in automatic mode, capturing photos based on the previously saved settings
- While waiting between photo captures, the ESP32-CAM enters deep sleep mode, and a DS3231 RTC module tracks time until the next wakeup to save power.
- Captured images are stored on a microSD card located at the top of the device. Be sure to format the card using the FAT32 file system for proper functionality.
- Two 18650 batteries are used to provide power.

### Webserver
- Here is a quick showcase of the Frontier TrailCam Webserver:
- Upon bootup, we pressed the button on the side of the TrailCam to get it to run the webserver, and we've connected to it's wifi network.
- We first land on the homepage, which gives an overview of all of the functionality.
- I then pressed on the "Stream" button, which takes us to the page where we can change the settings of the photos(i.e. resolution, saturation, etc). By clicking the "Start Stream" button, we get to see a live stream of the camera view.
- Next we navigate to the "Settings" page where we can change settings related to the photo taking schedule: what time/date to start taking photos, and what interval after that it should take a photo. After the settings are changed, we get a pop-up confirming the changes have been uploaded to the TrailCam.

[![Webserver Demo](https://github.com/user-attachments/assets/0e3d233f-7d71-49d6-9f52-8da293f8193f)](https://github.com/user-attachments/assets/edf6cd34-a822-4fc0-88a2-eb6a8e2fd074)
### Case
- The `TrailCam Case` folder includes CAD files and drafting sketches for the three pieces of the case:
- Top Cover: Mounts ESP32 cam, RTC module
- Bottom Cover: Mounts battery pack and charging circuitry
- Camera Cover: gives a nice aesthetic to the camera

### Electrical & Circuitry
- The main electrical components are:
- 1 x ESP32 CAM AI Thinker
- 1 x DPDT Switch
- 1 x SPST Button
- 1 x DS3231 RTC module
- 1 x TP4056 Battery Charging Board (USB-C & microUSB versions of the board are now available)
- 2 x 18650 Battery
- 2 x 18650 Battery Holder
- 1 x Boost Converter
- Wiring photo is shown in the `TrailCam Case` folder
- The batteries, their holders, and the charging module are mounted in the bottom part. The batteries are wired to the charging module. Cables from the charging module are attached to a boost converter, which steps up the voltage from the 3.7V 18650 batteries to the 5V input needed for the ESP32Cam. After going through the boost converter, the power cable is connected to the ESP32CAM. In the top part, the ESP32CAM, RTC module, and two switches are mounted. The DPDT switch is wired to the ESP32 cam and acts as the on button. The SPST button is wired to the ESP32CAM. When pushed, the ESP32CAM will enter the webserver mode. The RTC module keeps track of time while the ESP is waiting between photo-taking times. It's wired to  the ESP32Cam to send a signal to get it to exit deep sleep.

### Notes
Frontier TrailCam doesn't track the changes in daylight savings time. Must be off while charging. It also cannot take photos while it is being recharged. Battery life is estimated at about one month at a photo taking interval of one a day.
