# Frontier TrailCam
## Remotely accessible, Programmable, Arduino-based, Timelapse Camera Stations
<img width="982" alt="Screenshot 2025-05-02 at 12 46 38 AM" src="https://github.com/user-attachments/assets/25ab02a0-4f61-43b5-a421-9fa08e7b2a63" />
Frontier TrailCam is a simple, open-source timelapse camera system. It runs on the ESP32-CAM, a small, Arduino-compatible board equipped with a camera to take HD photos. Frontier TrailCam is compact and portable, giving it flexibility to be deployed in many applications, whether you're tracking changes in the invasive species at a park, watching flowers grow in your garden, or filming a sunset. With flexible firmware and a web interface for adjusting settings, it's a fun and practical open-source TrailCam for anyone needing a portable timelapse camera.

### Technical Overview
Frontier TrailCam is built around the ESP32-CAM, an Arduino-compatible system-on-chip (SoC). The device features two physical controls on its side: a power switch and a mode button. When powered on, pressing the mode button during startup launches the TrailCam’s built-in web server. Through this interface, users can adjust camera settings such as photo resolution and capture interval, as well as view a live feed to help position the camera. If the button is not pressed at startup, the camera will operate in automatic mode, capturing photos based on the previously saved settings. Captured images are stored on a microSD card located at the top of the device. Be sure to format the card using the FAT32 file system for proper functionality.

### Case
- The `TrailCam Case` includes CAD files and drafting sketches for the three pieces of the case
- Top Cover: Mounts ESP32 cam, RTC module
- Bottom Cover: Mounts battery pack and charging circuitry
- Camera Cover: gives nice aesthetic to the camera
