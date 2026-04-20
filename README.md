# Heliotrax-Reference Firmware

Heliotrax is a battery-less, solar-powered IoT platform for outdoor monitoring and control.

This repository provides reference firmware for the hardware platform, intended for:
- hardware testing and validation
- proof-of-concept implementations
- developer guidance

The platform is designed for developers and system integrators.  
Users are expected to build their own application software based on their requirements.

## Overview
This repository contains reference firmware for the Heliotrax hardware platform.

It is intended for:
- Hardware testing
- Proof of concept
- Customer guidance

## Important
This is NOT production-ready software.
Customers are expected to develop their own applications.

## Repository Structure

- `/transmitter` – firmware for the transmitter node  
- `/receiver` – firmware for the receiver node  

## Getting Started
1. Install Arduino IDE 
2. Open the transmitter or receiver project  
3. Configure hardware-specific parameters  
4. Upload firmware to the device  

> Note: This firmware is intended as a reference implementation and may require adaptation for your specific setup.

## Hardware

This firmware is designed for the Heliotrax hardware platform.

More information:
https://heliotrax.io

 transmitter - Heliotrax HW (j4g1.0 PCB and j4g_24v PCB required)
 
 receiver - any ESP32-C3 board (e.g. XIAO ESP32C3 or ESP32-C3 Dev Board) 

## Software Approach

Heliotrax follows a hardware-first approach.

We provide:
- robust hardware
- reference firmware

Users develop their own software solutions:
- fully flexible
- vendor-independent
- tailored to their application

## Technologies

- Arduino (current proof of concept)
- ESPHome / Home Assistant (planned concept)

## License
MIT License
