#pragma once

#include <Sensor.hpp>
#include <Adafruit_SGP30.h>

class SGP30 : public Sensor
{
private:
    Adafruit_SGP30 sgp;
    double data[2] = {0.0, 0.0};
    String descriptors[2] = {"eCO2 (ppm)", "TVOC (ppb)"};
    bool operational = false;

public:
    SGP30()
    {
    }

    int init()
    {
        if (!sgp.begin())
        {
            operational = false;
            return 1;
        }

        operational = true;
        return 0;
    }

    int getDataCount()
    {
        return 2;
    }

    bool isOperational()
    {
        return operational;
    }

    void readData()
    {
        if (!operational)
        {
            return;
        }

        if (!sgp.IAQmeasure())
        {
            operational = false;
            return;
        }

        data[0] = (double)sgp.eCO2;
        data[1] = (double)sgp.TVOC;
    }

    double *getData()
    {
        return data;
    }

    String getName()
    {
        return "SGP30";
    }

    String *getDescriptors()
    {
        return descriptors;
    }
};