#Kindel Systems Premise to External Control Processor Bridge
Copyright (c) 2012 Kindel Systems. All Rights Reserved.

This software is licensed under the [MIT](http://www.opensource.org/licenses/mit-license.php) license.

##Overview
This driver for the [Premise Home Control Software](http://cocoontech.com/forums/forum/51-premise-home-control/) (written in C++ using the Premise HSDK) implements what looks to Premise like a set of RS-232 ports. Each device connected to an external control processor (such as a Crestron processor) can appear as a "virtual" RS-232 port within Premise. In fact, anything that can be controlled with your external control processor can be made to look like a RS-232 device to Premise.

There is a single serial connection between Premise and the control processor. This driver effectively multiplexes many "virtual" serial (or IR) connections over that. Communication can be completely bi-directional. Future versions may support Ethernet connections to the control processor.

This driver was originally developed to enable Premise to interface with Crestron systems. However, the implementation is completely generic and will work with any device (typically a control processor) connected to Premise via RS-232 that supports a text based RS-232 protocol. The current implementation requires that the control processor can be programmed in some way to accept and send strings prefixed with some number of characters followed by a colon (":") followed by a command. E.g: "amp:vol+" would cause a volume up command against an amplifier device connected to the processor.

I'm very interested in hearing from people who have devices of this sort that this driver either works with or does not (because, for example, it uses a binary protocol or something where my "prefix:" scheme is incompatible).

##Files included
*sppkindel.dll    Premise Home Control Software driver for interfacing with an external control processor.
*Premise Device Serial IO Adapter.zip    Crestron SIMPL module for simplifying bridging to Crestron 2-series processors (will not work with x-gen processors).
*Crestron.ico   Icon you can copy to your Premise icons directory and use if your external processor is a Crestron unit.
*Sample.smw  Sample SIMPL Windows program illustrating how this bridge works with Crestron (will not work with x-gen processors).

##Installation

*Copy spkindel.dll to your \Program Files\Premise\SYS\bin\drivers directory.
*Restart the Premise service (not always necessary, but I've seen weirdness in Premise if you don't do this).
*In Premise Builder go to Add-Ins and enable the driver named "KindelSystems".
(You will now have a KindelSystems node in Devices.
*Unzip Premise Device Serial IO Adapter.zip to your SIMPL Windows Usrmacro and Userplus directories (the .umc file goes in Usermacro and the other files go in Userplus).
*In SIMPL Windows choose "Rebuild User and Project Databases" in the Tools menu.
*You will now have this symbol available in your symbol library under User Modules/Serial.

##Bug Reporting
*Use the GitHub Issues page at https://github.com/tig/spkindel/issues
*Use https://github.com/tig/spkindel/wiki for discussions & suggestions

##Release Notes
####Build 2.0.2 - November 9, 2003
This is the first build I am making available to beta testers. This has been tested thoroughly on my system, but has not undergone any testing on other systems. There are some known bugs around the closing of ports in Premise. If you see that you close a VirtualDevice port it may continue to show an open status. To fix this stop and restart the Premise service. You can also delete the VirtualDevice and re-create it.

The included spkindel.dll is a DEBUG build. It is recommended that you run a tool on your Premise server that can display/capture debug spew. I use DebugView from www.sysinternals.com (http://www.sysinternals.com/ntw2k/freeware/debugview.shtml). To view the debug spew from the Premise server choose Capture Global Win32 from the Capture menu in DebugView. Note that sometimes you have to choose this menu item TWICE due to an apparently bug in DebugView.

The sample Crestron files included work only with 2-series processors. These files are not strictly needed to utilize this driver, although the Premise Device Serial IO Adapter does make things much simpler when you are bridging to a serial device that has both Premise and Crestron drivers. I had this all working "manually" before I wrote the adapter so I know you can do it too . While I don't have an x-gen Crestron processor to test the driver with, I'm pretty confident it will work!

