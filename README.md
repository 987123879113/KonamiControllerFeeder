# KonamiControllerFeeder
vJoy Feeder program for Konami's IIDX and SDVX entry model controllers

## Setup
Create a joystick in vJoyConf with 10 buttons, the X axis, and the Y axis available. No other buttons are required.

## Building
Recommended to use the latest version of Visual Studio Community 2019.

Requires WinRT and Windows 10. Does not work on any version before Windows 10 Creators Update.

## Running
Running KonamiControllerFeeder.exe without a parameter will default to vJoy device 1.

You can optionally specify the device ID after KonamiControllerFeeder.exe, like so: `KonamiControllerFeeder.exe 4`.


## TODO
- [ ] Add a way to adjust sensitivity of turntable and knobs