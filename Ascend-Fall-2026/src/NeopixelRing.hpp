#pragma once

#include <NeoPixelBus.h>

class NeopixelRing
{
public:
    enum class Status
    {
        OFF,
        ON,
        BOOT,
        ERROR,
        SD_ERROR
    };

private:
    const uint16_t PixelCount = 12; // Number of NeoPixels in the ring
    const uint8_t PixelPin = 10;    // Pin where NeoPixels are connected
    NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip;
    volatile Status status = Status::OFF;
    volatile uint8_t brightness = 255; // 0-255 brightness scale
    uint32_t operationStartTime = 0;
    const uint32_t AUTO_OFF_TIME = 200000; // 200 seconds in milliseconds

    float animationCounter = 0.0f;

public:
    NeopixelRing() : strip(PixelCount, PixelPin)
    {
    }

    void begin()
    {
        strip.Begin();
        strip.ClearTo(RgbColor(0, 0, 0));
        strip.Show();
        operationStartTime = millis();
    }

    void setPixelColor(uint16_t index, RgbColor color)
    {
        if (index < PixelCount)
        {
            strip.SetPixelColor(index, color);
        }
    }

    void show()
    {
        strip.Show();
    }

    void setStatus(Status s)
    {
        status = s;
    }

    void setBrightness(uint8_t b)
    {
        brightness = b;
    }

    uint8_t getBrightness() const
    {
        return brightness;
    }

    Status getStatus() const
    {
        return status;
    }

    // Scale RGB color by brightness
    RgbColor scaleBrightness(RgbColor color) const
    {
        return RgbColor(
            (uint8_t)((color.R * brightness) / 255),
            (uint8_t)((color.G * brightness) / 255),
            (uint8_t)((color.B * brightness) / 255));
    }

    // Rainbow color from hue (0-360)
    RgbColor hsvToRgb(float hue, float saturation = 1.0f, float value = 1.0f) const
    {
        float c = value * saturation;
        float x = c * (1 - fabs(fmod(hue / 60.0f, 2) - 1));
        float m = value - c;

        float rp, gp, bp;
        if (hue < 60)
        {
            rp = c;
            gp = x;
            bp = 0;
        }
        else if (hue < 120)
        {
            rp = x;
            gp = c;
            bp = 0;
        }
        else if (hue < 180)
        {
            rp = 0;
            gp = c;
            bp = x;
        }
        else if (hue < 240)
        {
            rp = 0;
            gp = x;
            bp = c;
        }
        else if (hue < 300)
        {
            rp = x;
            gp = 0;
            bp = c;
        }
        else
        {
            rp = c;
            gp = 0;
            bp = x;
        }

        return RgbColor(
            (uint8_t)((rp + m) * 255),
            (uint8_t)((gp + m) * 255),
            (uint8_t)((bp + m) * 255));
    }

    // Rotating rainbow animation
    void animateRainbow()
    {
        for (uint16_t i = 0; i < PixelCount; i++)
        {
            float hue = fmod((i * 30.0f + animationCounter * 5.0f), 360.0f);
            RgbColor color = hsvToRgb(hue);
            setPixelColor(i, scaleBrightness(color));
        }
    }

    // Breathing red effect
    void animateBreathingRed()
    {
        float breath = (sin(animationCounter * 0.05f) + 1.0f) / 2.0f; // 0 to 1
        uint8_t intensity = (uint8_t)(breath * 255);
        RgbColor red(intensity, 0, 0);
        strip.ClearTo(scaleBrightness(red));
    }

    // Flashing yellow and red
    void animateSDError()
    {
        float phase = fmod(animationCounter * 0.08f, 1.0f);
        RgbColor color;

        if (phase < 0.5f)
        {
            color = RgbColor(255, 255, 0); // Yellow
        }
        else
        {
            color = RgbColor(255, 0, 0); // Red
        }

        strip.ClearTo(scaleBrightness(color));
    }

    // Blue boot animation
    void animateBoot()
    {
        float pulse = (sin(animationCounter * 0.03f) + 1.0f) / 2.0f; // 0 to 1
        uint8_t intensity = (uint8_t)(100 + pulse * 155);            // 100-255 range
        RgbColor blue(0, 0, intensity);
        strip.ClearTo(scaleBrightness(blue));
    }

    void update()
    {
        // Check if we should auto-off after 200 seconds
        if (millis() - operationStartTime > AUTO_OFF_TIME && status != Status::BOOT)
        {
            status = Status::OFF;
        }

        animationCounter += 1.0f;

        switch (status)
        {
        case Status::ON:
            animateRainbow();
            break;

        case Status::ERROR:
            animateBreathingRed();
            break;

        case Status::BOOT:
            animateBoot();
            break;

        case Status::SD_ERROR:
            animateSDError();
            break;

        case Status::OFF:
            strip.ClearTo(RgbColor(0, 0, 0));
            break;
        }

        show();
    }

    void clear()
    {
        strip.ClearTo(RgbColor(0, 0, 0));
        show();
    }
};