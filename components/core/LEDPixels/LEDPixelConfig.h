/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LedPixelConfig.h - Configuration for LED pixels
//
// Rob Dobson 2023-24
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftArduino.h"
#include "LEDPixel.h"
#include "RaftUtils.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"
#include "LEDStripConfig.h"
#include "LEDSegmentConfig.h"
#include "esp_idf_version.h"

class LEDPixelConfig
{
public:
    LEDPixelConfig()
    {
    }
    bool setup(const RaftJsonIF& config)
    {
        // LED Strip configs
        std::vector<String> stripConfigStrs;
        config.getArrayElems("strips", stripConfigStrs);

        // Check strip config size
        if (stripConfigStrs.size() == 0)
        {
            LOG_W(MODULE_PREFIX, "setup no LED strip configs");
            return false;
        }

        // Convert strip configs
        totalPixels = 0;
        stripConfigs.resize(stripConfigStrs.size());
        for (int stripIdx = 0; stripIdx < stripConfigStrs.size(); stripIdx++)
        {
            if (!stripConfigs[stripIdx].setup(RaftJson(stripConfigStrs[stripIdx])))
            {
                totalPixels = 0;
                LOG_W(MODULE_PREFIX, "setup strip config %d invalid", stripIdx);
                return false;
            }
            totalPixels += stripConfigs[stripIdx].numPixels;
        }

        // Segment configs
        std::vector<String> segmentConfigStrs;
        config.getArrayElems("segments", segmentConfigStrs);

        // Convert segment configs
        segmentConfigs.resize(segmentConfigStrs.size());
        for (int segIdx = 0; segIdx < segmentConfigStrs.size(); segIdx++)
        {
            if (!segmentConfigs[segIdx].setup(RaftJson(segmentConfigStrs[segIdx])))
            {
                LOG_W(MODULE_PREFIX, "setup segment config %d invalid", segIdx);
                return false;
            }
        }

        return true;
    }

    // LED strips
    uint32_t totalPixels = 0;
    std::vector<LEDStripConfig> stripConfigs;

    // LED segments
    std::vector<LEDSegmentConfig> segmentConfigs;

    // Debug
    static constexpr const char* MODULE_PREFIX = "LEDPixCfg";
};
