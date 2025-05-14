# HeltecV3-Lora Reaper Mesh

## Description

Reaper Mesh is a LoRa communication device specifically designed for Reaper Net but will soon be updated that is
can be used via a dedicated mobile devices.

To make this as easy as possible to use, I have designed serial commands in the AT+ formatted. There is no need for protobufs to use this product. As of now, the firmware is only for the HeltecV3 lora device. It currently works off of static variables for the device settings but this will soon be updated to by adjustable. The firmware is set to 915 for the frequency but I do plan on adding in the ability to set "channels" so that this can be changed to enter into a private channel.

The firmware does use AES encryption for messages out of the box. I undertsand this is not ideal but for now it is a place holder for stronger encryption which will be on a seperate device instead of on the device itself.

While bound to the rate limit, theroticlly there is no limit to the length of data that can be sent thanks to fragmentation. Each messsage is autopmaticlly broken 
into segments that are encrypted and can be peiced back together on the other side. I have tested a 3KB image and it worked fine but took some time. They data type you choose to use is important along with the file size.

## Techicals

**Commands**

- AT+MSG=YOUR MESSAGE HERE.
- AT+GPS=lat,lng

## Features

-   Long range communications for off-grid communication.

## Table of Contents

-   [Installation](#installation)
-   [Usage](#usage)
-   [Features](#features)
-   [Contributing](#contributing)
-   [License](#license)

## Installation

Provide instructions on how to install and set up the project. For example:

```bash
git clone https://github.com/yourusername/HeltecV3-Lora.git
cd HeltecV3-Lora
# Add additional setup steps here
```

## Usage

AT Commands

AT

AT+MSG=FromDesktopThisMessageIsFrom
AT+MSG=FromLaptop
AT+NAME=ReaperNodeDesktop
AT+NAME=ReaperNodeLaptop
AT+MSG=Here is a longer message to be sent using lora?
AT+MSG=Roger That
AT+MSG=Moving to location now
AT+MSG=I am at Rally Point Alpha. Fire when Ready. In position and ready for the next phase in the mission plan.
AT+MSG=Willing and Able. 3RD ID in place.
AT+MSG=Received
AT+GPS=38.8977,-77.0365

HIGH PRIORITY
AT+MSG=!THIS IS HIGH PRIOROTY


```cpp
// Example usage
#include <YourLibrary.h>

void setup() {
    // Initialization code
}

void loop() {
    // Main logic
}
```

## Features

-   Feature 1
-   Feature 2
-   Feature 3

## Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository.
2. Create a new branch (`git checkout -b feature-branch`).
3. Commit your changes (`git commit -m 'Add some feature'`).
4. Push to the branch (`git push origin feature-branch`).
5. Open a pull request.

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgments

-   Mention any resources, libraries, or contributors you'd like to thank.
