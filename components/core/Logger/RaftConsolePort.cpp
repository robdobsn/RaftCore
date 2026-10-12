/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftConsolePort
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftConsolePort.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_serial_output.h"
#endif

namespace
{
    std::atomic<bool> s_claimed{false};
    std::atomic<RaftConsoleTextSink> s_sink{nullptr};
    char s_owner[32] = {};

#ifdef ESP_PLATFORM
    vprintf_like_t s_prevVprintf = nullptr;
    bool s_stdoutRedirected = false;

    /// ESP-IDF log output while claimed: to the sink, or nowhere
    int claimedVprintf(const char* fmt, va_list args)
    {
        char buf[256];
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        if (len <= 0)
            return len;
        const uint32_t used = len < (int)sizeof(buf) ? (uint32_t)len : (uint32_t)sizeof(buf) - 1;
        RaftConsoleTextSink sink = s_sink.load();
        if (sink)
            sink(buf, used);
        return len;
    }

    /// ROM printf (esp_rom_printf, ESP_EARLY_LOG, ESP_DRAM_LOG - often from
    /// interrupt handlers) writes to the port's hardware directly; drop it
    void IRAM_ATTR nullPutc(char)
    {
    }
#endif
}

namespace RaftConsolePort
{

bool claim(const char* owner, RaftConsoleTextSink sink)
{
    if (!owner)
        return false;
    if (s_claimed.load())
    {
        if (strncmp(s_owner, owner, sizeof(s_owner)) != 0)
            return false;
        s_sink = sink;
        return true;
    }
    strncpy(s_owner, owner, sizeof(s_owner) - 1);
    s_owner[sizeof(s_owner) - 1] = '\0';
    s_sink = sink;

    // Stop Raft's own console output first (LoggerCore checks this flag)
    s_claimed = true;

#ifdef ESP_PLATFORM
    // ESP-IDF log output (ESP_LOGx) - keep whatever hook was installed (the
    // SerialConsole installs one) to put back on release
    s_prevVprintf = esp_log_set_vprintf(claimedVprintf);

    // stdout / stderr (one FILE object shared by every task) - anything that
    // printf()s directly
    fflush(stdout);
    fflush(stderr);
    s_stdoutRedirected = freopen("/dev/null", "w", stdout) != nullptr;

    // ROM printf channel
    esp_rom_install_channel_putc(1, nullPutc);
#endif
    return true;
}

bool release(const char* owner)
{
    if (!owner || !s_claimed.load() || strncmp(s_owner, owner, sizeof(s_owner)) != 0)
        return false;

#ifdef ESP_PLATFORM
    // ROM printf channel - as ESP-IDF's startup sets it for this console
#if CONFIG_ESP_CONSOLE_NONE
    esp_rom_install_channel_putc(1, NULL);
#elif !CONFIG_ESP_CONSOLE_USB_CDC
    esp_rom_install_uart_printf();
    esp_rom_output_set_as_console(CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM);
#endif

    // stdout / stderr back to the console
    if (s_stdoutRedirected)
    {
        fflush(stdout);
        freopen("/dev/console", "w", stdout);
        s_stdoutRedirected = false;
    }

    // ESP-IDF log output
    esp_log_set_vprintf(s_prevVprintf ? s_prevVprintf : vprintf);
    s_prevVprintf = nullptr;
#endif

    s_sink = nullptr;
    s_owner[0] = '\0';
    s_claimed = false;
    return true;
}

bool isClaimed()
{
    return s_claimed.load();
}

void setSink(RaftConsoleTextSink sink)
{
    if (s_claimed.load())
        s_sink = sink;
}

bool divert(const char* text, uint32_t len)
{
    if (!s_claimed.load())
        return false;
    RaftConsoleTextSink sink = s_sink.load();
    if (sink && text)
        sink(text, len);
    return true;
}

}
