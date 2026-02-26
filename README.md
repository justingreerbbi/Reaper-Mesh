# Reaper Mesh

Reaper Mesh is LoRa communication firmware for the Heltec WiFi LoRa 32 V3 (SX1262). It was originally developed to support the broader Reaper Net ecosystem and is designed to provide a simple, lightweight, serial-based interface for long-range device-to-device messaging.

The firmware uses a serial `AT+` command interface instead of a heavier protocol stack. Its goal is to remain approachable, portable, and extensible while still supporting encrypted message transport, fragmentation, acknowledgments, and future mesh capabilities.

## Overview

Reaper Mesh currently focuses on device-to-device communication over LoRa using Heltec V3 hardware. Messages are sent through a serial interface, fragmented as needed, encrypted, transmitted over the air, and reassembled on the receiving side.

The current implementation is intentionally simple. It supports serial communication today, with planned expansion into mobile integration, improved configuration, and more advanced networking features in future releases.

## Current Status

This project is in active development.

At the moment, Reaper Mesh supports:

- Heltec WiFi LoRa 32 V3 hardware
- Serial `AT+` command input
- Encrypted message transmission
- Message fragmentation and reassembly
- Group messaging
- Basic direct messaging support
- Retry and acknowledgment behavior

The current network model is single-hop. Full mesh routing is planned for a future release.

## Supported Hardware

Reaper Mesh currently supports:

- Heltec WiFi LoRa 32 V3 (SX1262)

Support for additional devices may be added later, but the project is still evolving and the protocol may change as development continues.

Official hardware page:

[Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/)

## Important Compatibility Note

Reaper Mesh uses its own protocol. It is not compatible with Meshtastic, LoRaWAN, or other standard LoRa communication stacks.

To communicate successfully, all devices must be running compatible versions of Reaper Mesh firmware.

## Features

### Available Now

- Serial `AT+` command interface
- Encrypted over-the-air messaging
- Message fragmentation and reassembly for larger payloads
- Group message transmission
- Direct message support
- Retry logic that continues until at least one node acknowledges receipt
- Transmission status output over serial for application-side monitoring

### Planned Features

- Full multi-hop mesh networking
- Runtime configuration of device settings
- Configurable frequencies and channels
- Private mesh/channel support
- Low-power operation
- Expanded node tracking
- OLED status display improvements
- Configurable encryption keys
- Wireless access point mode with REST API and UI
- BLE support for mobile integration
- External configuration loading, including SD card support where hardware allows
- Companion mobile application support

## Architecture Overview

```text
+--------------+        LoRa        +--------------+
|  Device A    | <----------------> |  Device B    |
| (Heltec V3)  |                    | (Heltec V3)  |
+--------------+                    +--------------+
      |                                   |
      | USB Serial                        | USB Serial
      v                                   v
  AT+ Commands                    AT+ Output + RX Events
      |                                   |
      |------ Encrypted LoRa Frames ----->|
      |<----- Encrypted LoRa Frames ------|
```

## Message Types

To keep the protocol simple, transmissions are wrapped in a small message-type structure. This allows devices to identify the purpose of a payload while keeping the send path unified.

Supported transmission types:

1. `MSG`
2. `BEACON`
3. `DMSG`

Examples:

- `MSG:This is a standard text message`
- `BEACON:lat,lng,alt,speed,direction,sat_num`
- `DMSG:TO|FROM|MSG|MSGID`

## Supported Commands

```text
AT+DEVICE?
AT+MSG=YOUR MESSAGE HERE
AT+DMSG=7065|Hello Node 7065. How are you today? This is a longer direct message.
AT+BEACON
AT+GPS?
```

### Long Message Test Example

```text
AT+MSG=Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque euismod, nisi eu consectetur cursus, enim erat dictum urna, nec dictum enim enim nec urna. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; Etiam euismod, enim nec dictum dictum, enim erat dictum urna, nec dictum enim enim nec urna. Vestibulum ante ipsum primis in faucibus orci luctus.
```

## Installation

### Clone the Repository

```bash
git clone https://github.com/justingreerbbi/Reaper-Mesh.git
```

### Recommended Development Setup

1. Install Visual Studio Code.
2. Install the PlatformIO extension.
3. Open the cloned project folder in VS Code.
4. Allow PlatformIO to install the required dependencies.
5. Connect your Heltec V3 device by USB.
6. Build and upload the firmware.
7. Open the serial monitor to interact with the device.

## Dependencies

- [RadioLib](https://github.com/jgromes/RadioLib)
- [Arduino-Crypto](https://rweather.github.io/arduinolibs/crypto.html)
- Heltec ESP32 board package

## Usage

Reaper Mesh is controlled through serial `AT+` commands.

### Example Commands

```text
AT+MSG=Hello World
AT+MSG=Moving to location now
AT+MSG=I am at Rally Point Alpha
AT+MSG=!HIGH PRIORITY MESSAGE
AT+GPS=38.8977,-77.0365
```

### Example Send Output

When a message is transmitted, the node returns status information over serial. This makes it easy for a desktop, mobile, or embedded application to monitor transmission progress.

Example:

```text
-> AT+MSG=Hello World
<- SENDING|MSGID|<MsgId>
<- SEND|FRAG|C16D|1/6|1/3
<- SEND|FRAG|C16D|2/6|1/3
<- SEND|FRAG|C16D|3/6|1/3
<- SEND|FRAG|C16D|4/6|1/3
<- SEND|FRAG|C16D|5/6|1/3
<- SEND|FRAG|C16D|6/6|1/3
<- ACK|CONFIRM|C16D
```

### Transmission Behavior

When a message is sent:

1. The device generates a message ID.
2. The message is fragmented if needed.
3. Each fragment is transmitted over LoRa.
4. Progress is reported over serial.
5. After transmission completes, the device listens for acknowledgment from other nodes.
6. If no acknowledgment is received, the node retries the broadcast.

This output is intended to make integration easier for client applications that want to display send progress, delivery confirmation, or retry state.

## Security Notes

All messages are currently encrypted before transmission. The current encryption approach is implemented on the device side and is intended to provide a baseline layer of message protection.

Future versions may move cryptographic operations into more specialized or hardened components and may introduce stronger key management and improved forward secrecy.

## Limitations

Current limitations include:

- Single-hop communication only
- Static configuration for some settings
- No runtime channel or key management yet
- No compatibility with other LoRa ecosystems such as Meshtastic or LoRaWAN
- No mobile application support yet
- No full mesh routing yet

## Contributing

Contributions are welcome.

To contribute:

1. Fork the repository.
2. Create a feature branch.

```bash
git checkout -b feature-name
```

3. Commit your changes.

```bash
git commit -m "Add new feature"
```

4. Push your branch.

```bash
git push origin feature-name
```

5. Open a pull request.

For larger changes, opening an issue first is recommended so the design can be discussed before implementation.

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgments

- [Heltec Automation](https://heltec.org/) for the hardware platform
- [RadioLib](https://github.com/jgromes/RadioLib) by Jan Gromes
- [Arduino-Crypto](https://rweather.github.io/arduinolibs/crypto.html)
- Contributors and testers involved in the Reaper Net ecosystem
