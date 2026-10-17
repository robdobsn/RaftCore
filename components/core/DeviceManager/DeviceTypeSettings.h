/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeSettings.h
//
// Settings for a device type chosen by the board or product rather than the device - the poll bus speed and the
// slots it applies to, the poll interval, samples kept, sample rate and scan priority - from the DevMan setting
// "typeSettings" in the SysType's configuration:
//
//   "typeSettings": { "LSM6DS": { "busHz": 400000, "busHzSlots": "1,2,3,4,7" }, "VL6180": { "scanPriority": "low" } }
//
// Names starting with "_" (e.g. "_notes") are ignored at both levels.
//
// The devconfig settings (the names devman/devconfig takes) are applied when a device of the type is identified,
// under the device's own settings (saved, or set while running), which override them. scanPriority is set on the
// type (the scanner's priority lists). See devdocs/device-type-record-management-plan.md section 3.9.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdlib.h>
#include <vector>
#include "RaftJson.h"
#include "DeviceTypeRecord.h"

class DeviceTypeSettings
{
public:
    // No scan priority set for the type
    static constexpr int SCAN_PRIORITY_UNSET = -1;

    struct Entry
    {
        String typeKey;
        std::vector<RaftJson::NameValuePair> devConfig;     // devconfig settings, values as text
        int scanPriority = SCAN_PRIORITY_UNSET;              // 0-2 (high, medium, low) or SCAN_PRIORITY_NONE
    };

    /// @brief Read the settings (replaces those held)
    /// @param config configuration holding the settings object
    /// @param pPath path of the settings object (e.g. "typeSettings")
    /// @param problems (out) problems found - unknown setting names and invalid values, which are left out
    void parse(const RaftJsonIF& config, const char* pPath, std::vector<String>& problems)
    {
        _entries.clear();
        std::vector<String> keys;
        config.getKeys(pPath, keys);
        for (const String& key : keys)
        {
            // Names starting with "_" are notes
            if (key.startsWith("_"))
                continue;
            Entry entry;
            entry.typeKey = key;
            String base = String(pPath) + "/" + key;
            std::vector<String> names;
            config.getKeys(base.c_str(), names);
            for (const String& name : names)
            {
                if (name.startsWith("_"))
                    continue;
                String path = base + "/" + name;
                String value;
                String why = readSetting(config, path.c_str(), name, value, entry.scanPriority);
                if (why.length() > 0)
                {
                    problems.push_back(key + "." + name + ": " + why);
                    continue;
                }
                if (name != "scanPriority")
                    entry.devConfig.push_back(RaftJson::NameValuePair(name, value));
            }
            _entries.push_back(entry);
        }
    }

    /// @brief The settings for a device type (nullptr if there are none)
    const Entry* find(const char* pTypeKey) const
    {
        if (!pTypeKey)
            return nullptr;
        for (const Entry& entry : _entries)
            if (entry.typeKey == pTypeKey)
                return &entry;
        return nullptr;
    }

    const std::vector<Entry>& all() const
    {
        return _entries;
    }

    /// @brief The devconfig settings to apply to a device: its type's, with the device's own replacing those with
    ///        the same name
    static std::vector<RaftJson::NameValuePair> layer(const std::vector<RaftJson::NameValuePair>& typeValues,
                const std::vector<RaftJson::NameValuePair>& deviceValues)
    {
        std::vector<RaftJson::NameValuePair> result;
        for (const RaftJson::NameValuePair& typeValue : typeValues)
        {
            bool replaced = false;
            for (const RaftJson::NameValuePair& deviceValue : deviceValues)
                replaced = replaced || (deviceValue.name == typeValue.name);
            if (!replaced)
                result.push_back(typeValue);
        }
        result.insert(result.end(), deviceValues.begin(), deviceValues.end());
        return result;
    }

    /// @brief Scan priority from its name: high, medium, low or none (-1 if not one of these)
    static int scanPriorityFromName(const String& name)
    {
        if (name.equalsIgnoreCase("high"))
            return 0;
        if (name.equalsIgnoreCase("medium"))
            return 1;
        if (name.equalsIgnoreCase("low"))
            return 2;
        if (name.equalsIgnoreCase("none"))
            return DeviceTypeRecord::SCAN_PRIORITY_NONE;
        return SCAN_PRIORITY_UNSET;
    }

private:
    std::vector<Entry> _entries;

    // A whole number in text, within limits
    static bool wholeNumber(const String& text, uint64_t minValue, uint64_t maxValue)
    {
        if ((text.length() == 0) || (text.length() > 19))
            return false;
        for (unsigned i = 0; i < text.length(); i++)
            if ((text[i] < '0') || (text[i] > '9'))
                return false;
        uint64_t value = strtoull(text.c_str(), nullptr, 10);
        return (value >= minValue) && (value <= maxValue);
    }

    // Read and check one setting; returns why it is invalid, or an empty string
    static String readSetting(const RaftJsonIF& config, const char* pPath, const String& name, String& value,
                int& scanPriority)
    {
        int arrayLen = 0;
        RaftJsonIF::RaftJsonType type = config.getType(pPath, arrayLen);
        if (name == "busHzSlots")
        {
            // A list of slot numbers: "1,2,3" or [1, 2, 3] ("" or "0" for all slots, as devconfig takes)
            if (type == RaftJsonIF::RAFT_JSON_ARRAY)
            {
                std::vector<String> elems;
                config.getArrayElems(pPath, elems);
                for (const String& elem : elems)
                {
                    if (value.length() > 0)
                        value += ",";
                    value += elem;
                }
            }
            else
            {
                value = config.getString(pPath, "");
            }
            String part;
            for (unsigned i = 0; i <= value.length(); i++)
            {
                if ((i == value.length()) || (value[i] == ','))
                {
                    part.trim();
                    if ((part.length() > 0) && !wholeNumber(part, 0, 63))
                        return "slot numbers 0 to 63 expected";
                    part = "";
                }
                else
                {
                    part += value[i];
                }
            }
            return "";
        }
        value = config.getString(pPath, "");
        if (name == "scanPriority")
        {
            scanPriority = scanPriorityFromName(value);
            return scanPriority == SCAN_PRIORITY_UNSET ? "high, medium, low or none expected" : "";
        }
        if (type == RaftJsonIF::RAFT_JSON_ARRAY || type == RaftJsonIF::RAFT_JSON_OBJECT)
            return "a number expected";
        if (name == "intervalUs")
            return wholeNumber(value, 1, UINT32_MAX) ? "" : "a whole number of microseconds above 0 expected";
        if (name == "numSamples")
            return wholeNumber(value, 1, 65535) ? "" : "a whole number above 0 expected";
        if (name == "busHz")
            return wholeNumber(value, 0, 10000000) ? "" : "a frequency in Hz expected (0 for the bus default)";
        if (name == "sampleRateHz")
            return (strtod(value.c_str(), nullptr) > 0) ? "" : "a rate above 0 expected";
        return "not a type setting (intervalUs, numSamples, busHz, busHzSlots, sampleRateHz or scanPriority)";
    }
};
