/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SampleCollector
// Collects samples in memory
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
#include <SysModBase.h>
#include <SpiramAwareAllocator.h>
#include <APISourceInfo.h>
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "FileSystem.h"

class SampleCollectorJSON : public SysModBase
{
public:
    SampleCollectorJSON(const char *pModuleName, ConfigBase &defaultConfig, ConfigBase *pGlobalConfig, ConfigBase *pMutableConfig)
        :   SysModBase(pModuleName, defaultConfig, pGlobalConfig, pMutableConfig)
    {
    }
    virtual ~SampleCollectorJSON()
    {
    }

    // Set sampling info
    // sampleRateLimitHz - limit on sample rate (0 = no limit)
    // pSampleHeader - header for samples (string - may be null - should be valid JSON if not)
    // pSampleAPIName - name of API for samples (string - may be null - in which case no API is created)
    // maxTotalJSONStringSize - max total JSON string size for all samples to be collected
    // allocateAtStart - allocate sample buffer at start
    // logToConsoleWhenFull - log to console when buffer full, clear buffer and start again
    void setSamplingInfo(uint32_t sampleRateLimitHz, 
            uint32_t maxTotalJSONStringSize,
            const char* pSampleHeader = nullptr, 
            const char* pSampleAPIName = nullptr, 
            bool allocateAtStart = true,
            bool logToConsoleWhenFull = false)
    {
        // Settings
        _sampleRateLimitHz = sampleRateLimitHz;
        if (pSampleHeader)
            _sampleHeader = pSampleHeader;
        else
            _sampleHeader = "SAMPLES";
        if (pSampleAPIName)
            _sampleAPIName = pSampleAPIName;
        _maxTotalJSONStringSize = maxTotalJSONStringSize;
        if (allocateAtStart)
            _sampleBuffer.reserve(_maxTotalJSONStringSize);
        if (_sampleRateLimitHz > 0)
            _minTimeBetweenSamplesUs = 1000000 / _sampleRateLimitHz;
        _logToConsoleWhenFull = logToConsoleWhenFull;
    }

    // Add sample
    bool addSample(const String& sampleJSON)
    {
        // Check if sampling enabled
        if (!_samplingEnabled)
            return false;

        // Check if buffer will be full
        if (_sampleBuffer.size() + sampleJSON.length() + 1 >= _maxTotalJSONStringSize)
        {
            // Dump to file
            if (_logToConsoleWhenFull)
            {
                writeToConsole();
                _sampleBuffer.clear();
            }
            else
            {
                return false;
            }
        }

        // Check time since last sample
        uint64_t timeNowUs = micros();
        if ((_minTimeBetweenSamplesUs != 0) && (Raft::isTimeout(timeNowUs, _timeSinceLastSampleUs, _minTimeBetweenSamplesUs)))
            return false;

        // Add sample to buffer
        _sampleBuffer.insert(_sampleBuffer.end(), sampleJSON.c_str(), sampleJSON.c_str() + sampleJSON.length());
        _sampleBuffer.push_back('\n');

        // Update time since last sample
        _timeSinceLastSampleUs = timeNowUs;
        return true;
    }

protected:

    // Setup
    virtual void setup() override final
    {}

    // Service
    virtual void service() override final
    {}

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final
    {
        // Check if API name defined
        if (_sampleAPIName.length() == 0)
            return;
        // Add endpoint for sampling
        pEndpoints.addEndpoint(_sampleAPIName.c_str(), 
                RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&SampleCollectorJSON::apiSample, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "handle samples, e.g. sample/start, sample/stop, sample/clear, sample/write/<filename>");
    }

    // Receive JSON command
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON) override final
    {
        addSample(cmdJSON);
        return RAFT_OK;
    }

private:
    // Sample API name
    String _sampleAPIName;

    // Header string
    String _sampleHeader;

    // Sample info
    uint32_t _sampleRateLimitHz = 0;
    uint32_t _maxTotalJSONStringSize = 0;
    bool _logToConsoleWhenFull = false;

    // Time since last sample
    uint64_t _timeSinceLastSampleUs = 0;
    uint64_t _minTimeBetweenSamplesUs = 0;

    // Enable/disable sampling
    bool _samplingEnabled = true;

    // Sample buffer
    std::vector<char, SpiramAwareAllocator<char>> _sampleBuffer;

    // API
    RaftRetCode apiSample(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
    {
        // Extract params
        std::vector<String> params;
        std::vector<RaftJson::NameValuePair> nameValues;
        RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
        String paramsJSON = RaftJson::getJSONFromNVPairs(nameValues, true);

        // Handle commands
        bool rslt = true;
        String rsltStr;
        if (params.size() > 0)
        {
            // Start
            if (params[1].equalsIgnoreCase("start"))
            {
                _samplingEnabled = true;
                rsltStr = "Ok";
            }
            // Stop
            else if (params[1].equalsIgnoreCase("stop"))
            {
                _samplingEnabled = false;
                rsltStr = "Ok";
            }
            // Clear buffer
            else if (params[1].equalsIgnoreCase("clear"))
            {
                _sampleBuffer.clear();
                rsltStr = "Ok";
            }
            // Write to file
            else if (params[1].equalsIgnoreCase("write"))
            {
                rslt = writeToFile(params[2], rsltStr);
            }
            // Get buffer
            else if (params[1].equalsIgnoreCase("get"))
            {
                respStr = String(_sampleBuffer.data(), _sampleBuffer.size());
                _sampleBuffer.clear();
                return RAFT_OK;
            }
        }
        // Result
        if (rslt)
        {
            // LOG_I(MODULE_PREFIX, "apiSample: reqStr %s rslt %s", reqStr.c_str(), rsltStr.c_str());
            return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
        }
        // LOG_E(MODULE_PREFIX, "apiSample: FAILED reqStr %s rslt %s", reqStr.c_str(), rsltStr.c_str());
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, rsltStr.c_str());
    }

    // Write to file
    bool writeToFile(const String& filename, String& errMsg)
    {
        // Open file
        FILE* pFile = fileSystem.fileOpen("", filename, true, 0);
        if (!pFile)
        {
            errMsg = "failOpen";
            return false;
        }

        // Write header
        uint32_t bytesWritten = fileSystem.fileWrite(pFile, (uint8_t*)_sampleHeader.c_str(), _sampleHeader.length());
        fileSystem.fileWrite(pFile, (uint8_t*)"\n", 1);
        bool rslt = true;
        if (bytesWritten != _sampleHeader.length() + 1)
        {
            errMsg = "failWrite";
            rslt = false;
        }

        // Write buffer
        if (rslt)
        {
            bytesWritten = fileSystem.fileWrite(pFile, (uint8_t*)_sampleBuffer.data(), _sampleBuffer.size());
            if (bytesWritten != _sampleBuffer.size())
            {
                errMsg = "failWrite";
                rslt = false;
            }
        }

        // Close file
        fileSystem.fileClose(pFile, "", filename, true);

        // Clear buffer
        _sampleBuffer.clear();

        // Return
        return rslt;
    }

    void writeToConsole()
    {
        // Write header
        LOG_I("S", "SampleCollector: %s", _sampleHeader.c_str());

        // Write lines
        uint32_t strPos = 0;
        while (strPos < _sampleBuffer.size())
        {
            // Find end of line
            uint32_t strPosEnd = strPos;
            while ((strPosEnd < _sampleBuffer.size()) && (_sampleBuffer[strPosEnd] != '\n'))
                strPosEnd++;
            // Write line
            LOG_I("S", "%.*s", strPosEnd - strPos, &_sampleBuffer[strPos]);
            // Next line
            strPos = strPosEnd + 1;
        }
    }
};
