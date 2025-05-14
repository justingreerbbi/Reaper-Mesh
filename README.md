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

Thanks to message fragmentation and reassembly, messages of virtually any length can be transmitted. Payloads are split, encrypted, and reassembled seamlessly. For example, a 3KB image was successfully sent over LoRa — albeit slowly — proving this firmware's support for large data transfers.

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

### Message Types

-   `AT+MSG=` – Sends a secure text message.
-   `AT+GPS=` – Sends encrypted GPS coordinates.

---

## Features

-   AES-encrypted long-range LoRa communication
-   Compact binary protocol with automatic fragmentation
-   Simple serial `AT+` command interface
-   Currently supports Heltec WiFi LoRa 32 V3 (SX1262)
-   No external libraries required for parsing
-   Designed for off-grid, tactical, or remote operations

---

## Table of Contents

-   [Installation](#installation)
-   [Usage](#usage)
-   [Features](#features)
-   [Contributing](#contributing)
-   [License](#license)
-   [Acknowledgments](#acknowledgments)

---

## Installation

```bash
git https://github.com/justingreerbbi/Reaper-Net/
cd Reaper-Net
# Upload using PlatformIO or Arduino IDE
```

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
