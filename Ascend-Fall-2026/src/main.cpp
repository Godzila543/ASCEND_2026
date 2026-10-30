///////////////////////////////////////////////////////////////
// UA SEDS ASCEND FALL 2024
// AUTHORS: Colin Brown
// PURPOSE: Main Control Code
///////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <Wire.h>
#include "CNS.hpp"

CNS cns;
void setup()
{
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(25000); // Set I2C frequency to 400kHz
    SPI.begin();

    cns.initializeSD();
    // cns.initializeMux();
    cns.initializeSensors();
    cns.printOperationalSensors();
    cns.printFailedSensors();
    cns.createCSVHeaders();
}

int loops = 0;
void loop()
{
    Serial.println("Starting Data Read Cycle");
    // Animation is now handled by Core 1, no need to call here
    cns.readSensorData();
    cns.printSensorData();
    cns.refreshSD();
}

// Core 1 setup - Initialize animation on second core
void setup1()
{
    delay(100); // Give Core 0 time to initialize
    Serial.println("Core 1: Animation thread started");
    cns.getNeopixelRing().begin();
    cns.getNeopixelRing().setBrightness(50);
    cns.getNeopixelRing().setStatus(NeopixelRing::Status::BOOT);
    cns.getNeopixelRing().update();
}

// Core 1 loop - High-frequency animation updates (60+ FPS)
void loop1()
{
    // Continuously update the NeoPixel animation
    cns.getNeopixelRing().update();

    // ~16ms delay for ~60 FPS animation
    delayMicroseconds(16000);
}
