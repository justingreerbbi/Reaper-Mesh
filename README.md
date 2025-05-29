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
AT+DEVICE?
AT+MSG=YOUR MESSAGE HERE
AT+DMSG=7065|Hello Node 7065. How are you today? This a bit of a long message.
AT+BEACON
AT+GPS?
```
Testing
```text
Long Message Test
AT+MSG=Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque euismod, nisi eu consectetur cursus, enim erat dictum urna, nec dictum enim enim nec urna. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; Etiam euismod, enim nec dictum dictum, enim erat dictum urna, nec dictum enim enim nec urna. Vestibulum ante ipsum primis in faucibus orci luctus.
```

---

## Features

-   Group Messaging & Direct Messaging
-   Fragmented transmission that allows for longer air time and larger data transmission.
-   Serial `AT+` command interface.
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

### BASIC MESSAGE STRUCTURE

Given the need to keep the code as clean as possible, I am changing up how different message types are going to be sent. This way we can use one method to send encrypted messages while still being to tell other devices, what we are sending.

Transmissions can be the following:

1. MSG:
2. BEACON:
3. DMSG:

When sending a text message simply append the appropriate type in the beginning of the message field. This will tell the other devices what type of message this is.

-   MSG:This is a normal text message and should be treated as such.
-   BEACON:lat,lng,alt,speed,direction,sat_num
-   DMSG:TO|FROM|MSG|MSGID

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

Commands sent that tranmit will result in an immediate response back from the Reaper Node. Below is an example of sending
global message, followed by the response back from the Reaper Node.

-> AT+MSG=Hellow World
<- SENDING|MSGID|<MsgId>
<- SEND|FRAG|C16D|1/6|try=1
<- SEND|FRAG|C16D|2/6|try=1
<- SEND|FRAG|C16D|3/6|try=1
<- SEND|FRAG|C16D|4/6|try=1
<- SEND|FRAG|C16D|5/6|try=1
<- SEND|FRAG|C16D|6/6|try=1
<- ACK|CONFIRM|C16D

The Reaper Node will respond with the Message ID that will be fragmented and broadcasted. For smaller messages this is quick
but this allows for monitoring by an application for larger messages. The node will present the current fragment being broadcasted
giving the status/progress of the send. Application can tap into the responses and show a progress bar if they choose to do so.

After the intial broadcast completion, the node will wait a few seconds to listen back for any node that acknolodes it. If it does
not hear back, the node will sttempt to retry the entire broadcast again. The index of the current try/retry in at the end of the response.

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
