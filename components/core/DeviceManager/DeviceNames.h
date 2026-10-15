/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceNames.h
//
// Device names and device roles, keyed by device ID.
//
// A device name is a user-assigned label for a device. Names are one-to-one with device IDs: each
// device ID has at most one name and each name belongs to at most one device, so assigning a name
// that is already in use moves it to the new device. A device role (e.g. "system") marks devices
// that need different treatment; a device with no role has the role "normal".
//
// Used by DeviceManager. Kept separate so the behaviour can be unit tested without a DeviceManager.
//
// Threading: not thread-safe; the caller provides any locking needed.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "RaftArduino.h"
#include "RaftDeviceConsts.h"
#include "RaftJsonIF.h"

class DeviceNames
{
public:
    /// @brief Load device names from configuration
    /// @param config Configuration with a "DeviceNames" object mapping device ID strings to names,
    ///        e.g. {"DeviceNames":{"1_f8":"accelerometer"}}. Invalid device IDs and empty names are skipped
    void loadFromConfig(const RaftJsonIF& config)
    {
        std::vector<String> keys;
        if (!config.getKeys("DeviceNames", keys) || keys.empty())
            return;
        for (const auto& key : keys)
        {
            String path = "DeviceNames/" + key;
            String name = config.getString(path.c_str(), "");
            if (name.length() == 0)
                continue;
            RaftDeviceID deviceID = RaftDeviceID::fromString(key);
            if (!deviceID.isValid())
                continue;
            setName(deviceID, name);
        }
    }

    /// @brief Assign a name to a device, replacing any name it had and taking the name from any other
    ///        device that had it
    /// @param deviceID Device ID
    /// @param name Device name
    void setName(RaftDeviceID deviceID, const String& name)
    {
        std::string nameStr(name.c_str());
        uint64_t key = packDeviceIDKey(deviceID);

        // Remove old name for this device ID if it exists
        auto itOld = _deviceIDToName.find(key);
        if (itOld != _deviceIDToName.end())
        {
            _deviceNameToID.erase(itOld->second);
            _deviceIDToName.erase(itOld);
        }

        // Remove old device ID for this name if it exists
        auto itOldName = _deviceNameToID.find(nameStr);
        if (itOldName != _deviceNameToID.end())
        {
            _deviceIDToName.erase(packDeviceIDKey(itOldName->second));
            _deviceNameToID.erase(itOldName);
        }

        // Store new mapping
        _deviceNameToID[nameStr] = deviceID;
        _deviceIDToName[key] = nameStr;
    }

    /// @brief Get the name of a device
    /// @param deviceID Device ID
    /// @return device name if set, otherwise deviceID.toString()
    String getNameForID(RaftDeviceID deviceID) const
    {
        auto it = _deviceIDToName.find(packDeviceIDKey(deviceID));
        if (it != _deviceIDToName.end())
            return String(it->second.c_str());
        return deviceID.toString();
    }

    /// @brief Get the name of a device without the device ID fallback
    /// @param deviceID Device ID
    /// @return device name if set, otherwise an empty string
    String lookupName(RaftDeviceID deviceID) const
    {
        auto it = _deviceIDToName.find(packDeviceIDKey(deviceID));
        if (it != _deviceIDToName.end())
            return String(it->second.c_str());
        return String();
    }

    /// @brief Find the device that has a name
    /// @param name Device name
    /// @param deviceID (out) device ID
    /// @return true if a device has this name
    bool resolveName(const String& name, RaftDeviceID& deviceID) const
    {
        auto it = _deviceNameToID.find(std::string(name.c_str()));
        if (it == _deviceNameToID.end())
            return false;
        deviceID = it->second;
        return true;
    }

    /// @brief Assign a role to a device
    /// @param deviceID Device ID
    /// @param role Role (e.g. "system"); empty or "normal" (any case) removes the role
    void setRole(RaftDeviceID deviceID, const String& role)
    {
        uint64_t key = packDeviceIDKey(deviceID);
        if (role.length() == 0 || role.equalsIgnoreCase("normal"))
        {
            _deviceRole.erase(key);
            return;
        }
        _deviceRole[key] = std::string(role.c_str());
    }

    /// @brief Get the role of a device
    /// @param deviceID Device ID
    /// @return role, or "normal" if none is set
    String getRole(RaftDeviceID deviceID) const
    {
        auto it = _deviceRole.find(packDeviceIDKey(deviceID));
        if (it != _deviceRole.end())
            return String(it->second.c_str());
        return String("normal");
    }

    /// @brief Check whether a device has the role "system" (case-sensitive)
    bool isSystem(RaftDeviceID deviceID) const
    {
        auto it = _deviceRole.find(packDeviceIDKey(deviceID));
        if (it == _deviceRole.end())
            return false;
        return it->second == "system";
    }

    /// @brief Number of named devices
    uint32_t nameCount() const
    {
        return _deviceIDToName.size();
    }

    /// @brief Number of devices with a role other than "normal"
    uint32_t roleCount() const
    {
        return _deviceRole.size();
    }

    /// @brief Pack a device ID into a key for map lookup
    static uint64_t packDeviceIDKey(RaftDeviceID deviceID)
    {
        return (static_cast<uint64_t>(deviceID.getBusNum()) << 32) | deviceID.getAddress();
    }

private:
    // Device name -> device ID
    std::unordered_map<std::string, RaftDeviceID> _deviceNameToID;
    // Packed device ID -> device name
    std::unordered_map<uint64_t, std::string> _deviceIDToName;
    // Packed device ID -> role (no entry means "normal")
    std::unordered_map<uint64_t, std::string> _deviceRole;
};
