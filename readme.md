# Smartplug

## The system

Let's start with an outline which describes the whole system in which the smartplug is used:

![Smartplug system](doc/img/System.png?raw=true "System")

There is a main smartplug called gateway which gathers data (power samples and relay status) from other smartplugs 
over Power Line Communication. The gateway is connected to local WiFi access point to be connected with remote
Thingsboard server which allows to control the system (switching on/off the devices connected to smartplugs in
the system) and visualize collected data on neat charts.

## The device

This simple outline shows what's inside the device:

![Smartplug simplified](doc/img/Device.png?raw=true "Device")
