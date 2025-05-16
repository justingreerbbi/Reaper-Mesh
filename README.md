# HeltecV3-Lora Reaper Mesh

![Platform](https://img.shields.io/badge/platform-Heltec%20V3-blue)
![License](https://img.shields.io/github/license/yourusername/HeltecV3-Lora)
![LoRa](https://img.shields.io/badge/LoRa-enabled-green)
![Encryption](https://img.shields.io/badge/encryption-AES128-informational)
![Status](https://img.shields.io/badge/status-in%20development-yellow)

---

## Description

**Reaper Mesh** is a LoRa-based communication firmware built for the Heltec WiFi LoRa 32 V3 (SX1262) device. Initially developed for **Reaper Net**, this firmware will soon support dedicated mobile device integration.

The firmware uses a lightweight, serial `AT+` command interface — no Protobufs, no complex protocols. It is designed for ease of use and extensibility. Current settings like frequency and device name are static, but future updates will support runtime configurability and private mesh channels.

Out of the box, all messages are **AES-encrypted**. While this initial encryption is device-side, future versions will offload encryption to dedicated cryptographic modules for improved security.

Thanks to built-in message fragmentation and reassembly, payloads of virtually any length can be transmitted. Messages are split, encrypted, and seamlessly reconstructed on the receiving end. For example, a 3KB image was successfully transmitted over LoRa — albeit slowly — demonstrating the system’s capability to handle large data.

Currently, the firmware supports only serial communication. However, as noted earlier, future updates will introduce support for a companion mobile app and potentially a self-hosted message server. I built this firmware with simplicity in mind — avoiding feature creep and unnecessary bloat. That said, I acknowledge that even a minimalist design like this may still pose a learning curve for some users. Once the core system logic is finalized, I plan to improve usability for broader accessibility.

Regarding mesh networking: the current implementation is single-hop, but full mesh functionality is a top priority for the next major update. Mesh networking is essential — and it’s coming, I promise.

**Supported Devices**
Currently there is only one device supported but this can easily be ported to other devices. I would encourage waiting until the project has longer legs. Until then, you can find the device on the official website below. You can pick these up on Amazon as well but make sure it is the real deal and not a clone.

Note: This firmware is its own protocol and will not work to communicate with other LoRa protocals like Meshtatstic or standard LoRa or LoRaWan. You will need two devices with teh same firmware for them to communicate.

[Buy the HeltecV3 Device](https://heltec.org/project/wifi-lora-32-v3/)

---

## Table of Contents

-   [Installation](#installation)
-   [Usage](#usage)
-   [Features](#features)
-   [Contributing](#contributing)
-   [License](#license)
-   [Acknowledgments](#acknowledgments)

---

## 🚀 Architecture Overview (WIP)

```
+--------------+        LoRa        +--------------+
|  Device A    | <----------------> |  Device B    |
| (Heltec V3)  |                   | (Heltec V3)  |
+--------------+                   +--------------+
      |                                  |
      | USB Serial                       | USB Serial
      v                                  v
  AT+ Commands                   AT+ Output + Msg RX
      |                                  |
      |----- AES128 Encrypted Msgs ----->|
      |<---- AES128 Encrypted Msgs ------|
```

---

### Supported Commands

```text
AT+MSG=YOUR MESSAGE HERE
AT+GPS=latitude,longitude
```

**EXAMPLES**

-   AT+MSG=Aplha Team in place. Processding to next objective.

### Message Types

-   `AT+MSG=` – Sends a secure text message.
-   `AT+GPS=` – Sends encrypted GPS coordinates.
-   `AT+RESET_DEVICE` - Reset the device settings and memory.

---

## Features

-   AES-encrypted messages.
-   Binary Fragmentation is used to be able to transmit data over the noraml limits of LoRa.
-   Simple serial `AT+` command interface with formatted responses for software integration.
-   Integrated retry system until at least one node acknowledges the message.

**Upcoming**

-   Low-power mode.
-   Multiple channel support.
-   Direct messaging.
-   Multiple node tracking.
-   OLED Information.
-   Ability to change settings including encryption keys.
-   Built in wireless Access Point with REST API and user interface.
-   Ability to load channel and keys via SD card (requires hardware development).
-   BLE connectivity with custom mobile application.

---

## How to use

This firmware provides no user interface to the device currently. Everything is done via Serial communications on Baud rate 115200. Connecting to the device can be done via a USB cable or through serial connections from another MCU.

As the project becomes more stable, I will be adding more ways to take advantage of the communication between nodes.

## Understand Serial Responses

Each response from the device has a simple format it follows. All responses are separated by the character "|". The first index will be LOG,RECV,SEND, or ERR.

**Receiving LOG Example**

LOG responses can be used to hook into or simply display as a log in your application. They give a keyword of what the LOG is for.

-   `LOG|DEVICE_CONNECTED`
-   `LOG|BEACON_SENT`

**Receiving a Beacon Example**

The example below shows a normal message coming in but after decrypting, the content is a beacon ping from another node. They are saying "Hi, I'm Here!"

-   `RECV|FRAG|F305|1/1`
-   `RECV|28C6|BEACON|F305`

**SEND/RECV Example**

Below is an example of sending a message that has 5 fragments followed by a `RECV|ACK_CONFIRM`. The ACK_CONFIRM is a receipt confirmation that a node as received the message.

AT+ command to send a message

-   `AT+MSG=Hello World`

Serial Output

-   `SEND|FRAG|56FA|1/5|TRY=1`
-   `SEND|FRAG|56FA|2/5|TRY=1`
-   `SEND|FRAG|56FA|3/5|TRY=1`
-   `SEND|FRAG|56FA|4/5|TRY=1`
-   `SEND|FRAG|56FA|5/5|TRY=1`
-   `RECV|ACK_CONFIRM|56FA`

NOTE: the RECV|ACK_CONFIRM response as the MSG_ID to let you know which message they confirmed they received.

### BASIC MESSAGE STRUCTURE

Given the need to keep the code as clean as possible, I am changing up how different message types are going to be sent. This way we can use one method to send encrypted messages while still being to tell other devices, what we are sending.

Trasnmissions can be the following:

1. MSG:
2. BEACON:
4. REQ:

When sending a text meesage simply append the appropriate type in the begining of the message field. This will tell the other devices what type of message this is.

- MSG:This is a normal text message and should be treated as such.
- BEACON:lat,lng,alt,speed,direction,sat_num
- REQ:BEACON

Note: BEACON will contain a set number of "," with each dedicated to a specific data point. Right now it is all GPS data but we should add more info like battery_lvl,device_type

## Installation

```bash
git https://github.com/justingreerbbi/Reaper-Net/
```

1. I prefer to use VSCode for this along with PlatformIO. If you don't have VSCode, download it and install PlatFormIO in the extensions. Once PlatformIO is installed, simple open a new project and open the directory where you downloaded this code. The system should download all the dependencies (which is not a lot).

2. Plug in your HeltecV3 device and upload and monitor.

3. In the monitor, you can copy and paste any of the example commands and the device will do the rest.

### Dependencies

-   [RadioLib](https://github.com/jgromes/RadioLib)
-   [Arduino-Crypto](https://rweather.github.io/arduinolibs/crypto.html)
-   Board package: Heltec ESP32 Boards

---

## Usage

### Example AT Commands

```text
AT+MSG=Hello World
AT+MSG=Moving to location now
AT+MSG=I am at Rally Point Alpha. Fire when ready.
AT+MSG=!THIS IS A HIGH PRIORITY MESSAGE
AT+GPS=38.8977,-77.0365
```

**UPDATE SETTINGS**

```text
SET|<key>|<value>
----
AT+SET=name,DT45
AT+SET=freq,915.0
AT+SET=power,22
AT+SET=maxret,2
AT+SET=retryint,1000
```

Set bulk settings

```text
AT+SETA=name,NODE2;freq,915.0;power,22;maxret,2;retryint,1000
```

---

## Todos

- Add reporting device -> Reaper Net with all connected PRN satilites. This will allow the interface to know, track, and provide detailed information abotu the sats it is using.
- Add AT command to get a list of all satilite information (GPS attached, num of sats, list of PRN's)
- Add AT command to get GPS location.
- Move the satilite struct and sat map logic to the user interface.

- The HeltecV3 has 2 cores. Build test firmeware with FreeRTOS so we can start deligating tasks and start multitasking.

---

## Contributing

Contributions are welcome! To contribute:

1. Fork this repository.
2. Create a new feature branch: `git checkout -b feature-name`
3. Commit your changes: `git commit -m "Add new feature"`
4. Push to GitHub: `git push origin feature-name`
5. Open a pull request.

---

## License

This project is licensed under the [MIT License](LICENSE).

---

## Acknowledgments

-   [Heltec Automation](https://heltec.org/) for hardware.
-   [RadioLib](https://github.com/jgromes/RadioLib) by Jan Gromes.
-   [Arduino-Crypto](https://rweather.github.io/arduinolibs/crypto.html) by rweather.
-   Contributors and testers from the Reaper Net project.
