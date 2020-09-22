# KonamiControllerFeeder
vJoy Feeder program for Konami's IIDX and SDVX entry model controllers

## Setup
Create a joystick in vJoyConf with 10 buttons, the X axis, and the Y axis available. No other buttons are required.

## Building
Recommended to use the latest version of Visual Studio Community 2019.

Requires WinRT and Windows 10. Does not work on any version before Windows 10 Creators Update.

## Running
Running KonamiControllerFeeder.exe without a parameter will default to vJoy device 1.

There are various parameters you can adjust by specifying them as arguments when executing the program:
```
usage: KonamiControllerFeeder.exe [--sensitivity-x 1.0] [--sensitivity-y 1.0] [--device-id 1] [--digital] [--help]

arguments:
        --device-id (val) - Set the target vJoy device ID
        --sensitivity-x (val) - Set the sensitivity of the X axis for analog mode
        --sensitivity-y (val) - Set the sensitivity of the Y axis for analog mode
        --digital - Turn X and Y axis values into digital instead of analog values. Useful for BMS simulators.
        --help - Display this help message
```
