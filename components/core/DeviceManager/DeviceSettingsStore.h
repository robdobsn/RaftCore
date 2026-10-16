/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceSettingsStore.h
//
// Settings saved for devices, keyed by device ID (bus, slot and address - so they belong to the position, not the
// physical device): a name and role, devconfig settings (with the device type they were made for - they apply
// only to a device of that type), and whether the device is polled. Held as JSON in a file:
//
//   {"devices":{"1_68":{"name":"imu","role":"system","type":"MPU6050","devconfig":{"intervalUs":"20000"},
//               "poll":false}}}
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <string>
#include <vector>
#include "RaftJson.h"

class DeviceSettingsStore
{
public:
    struct Settings
    {
        String name;
        String role;
        String typeKey;                                 // type the devconfig settings are for
        std::vector<RaftJson::NameValuePair> devConfig;
        int8_t pollEnabled = -1;                        // -1 not set, 0 not polled, 1 polled
        bool empty() const
        {
            return (name.length() == 0) && (role.length() == 0) && devConfig.empty() && (pollEnabled < 0);
        }
    };

    /// @brief Load from JSON (replaces the settings held)
    /// @return false if the JSON isn't a settings document (nothing is loaded)
    bool loadJson(const char* pJson)
    {
        _settings.clear();
        if (!pJson || !pJson[0])
            return true;
        RaftJson doc(pJson, false);
        std::vector<String> ids;
        if (!doc.getKeys("devices", ids))
            return false;
        for (const String& id : ids)
        {
            String base = "devices/" + id + "/";
            Settings settings;
            settings.name = doc.getString((base + "name").c_str(), "");
            settings.role = doc.getString((base + "role").c_str(), "");
            settings.typeKey = doc.getString((base + "type").c_str(), "");
            std::vector<String> configKeys;
            doc.getKeys((base + "devconfig").c_str(), configKeys);
            for (const String& key : configKeys)
                settings.devConfig.push_back(RaftJson::NameValuePair(key, doc.getString((base + "devconfig/" + key).c_str(), "")));
            int arrayLen = 0;
            if (doc.getType((base + "poll").c_str(), arrayLen) == RaftJsonIF::RAFT_JSON_BOOLEAN)
                settings.pollEnabled = doc.getBool((base + "poll").c_str(), true) ? 1 : 0;
            if (!settings.empty())
                _settings[id.c_str()] = settings;
        }
        return true;
    }

    /// @brief The settings as JSON
    String toJson() const
    {
        String json = "{\"devices\":{";
        bool first = true;
        for (const auto& entry : _settings)
        {
            const Settings& settings = entry.second;
            if (settings.empty())
                continue;
            String dev = "{";
            if (settings.name.length() > 0)
                RaftJson::appendStringField(dev, "name", settings.name.c_str());
            if (settings.role.length() > 0)
                RaftJson::appendStringField(dev, "role", settings.role.c_str());
            if (!settings.devConfig.empty())
            {
                RaftJson::appendStringField(dev, "type", settings.typeKey.c_str());
                String config = "{";
                for (const auto& nv : settings.devConfig)
                    RaftJson::appendStringField(config, nv.name.c_str(), nv.value.c_str());
                dev += String(dev.length() > 1 ? "," : "") + "\"devconfig\":" + config + "}";
            }
            if (settings.pollEnabled >= 0)
                dev += String(dev.length() > 1 ? "," : "") + "\"poll\":" + (settings.pollEnabled ? "true" : "false");
            dev += "}";
            String key;
            RaftJson::appendStringField(key, "k", entry.first.c_str());
            json += String(first ? "" : ",") + key.substring(4) + ":" + dev;
            first = false;
        }
        return json + "}}";
    }

    /// @brief The settings for a device (nullptr if none)
    const Settings* find(const String& deviceIdStr) const
    {
        auto it = _settings.find(deviceIdStr.c_str());
        return it != _settings.end() ? &it->second : nullptr;
    }

    /// @brief The settings for a device, added if there are none
    Settings& getOrAdd(const String& deviceIdStr)
    {
        return _settings[deviceIdStr.c_str()];
    }

    /// @brief All the settings
    const std::map<std::string, Settings>& all() const
    {
        return _settings;
    }

private:
    std::map<std::string, Settings> _settings;
};
