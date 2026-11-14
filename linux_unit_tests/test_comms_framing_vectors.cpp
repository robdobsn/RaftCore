/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Comms framing vectors: HDLC (RICSerial) and OverAscii (serial console)
//
// Makes the shared vectors scripts/tests/hdlc_vectors.json and scripts/tests/overascii_vectors.json from the
// firmware's own MiniHDLC and ProtocolOverAscii, so that client libraries (raftjs, raftpy) frame and unframe
// exactly as the firmware does. Run as a test (make check) it makes the vectors again and fails if the files
// differ, so a change to the framing code shows up as a change to the vectors.
//
//   ./test_comms_framing_vectors                      check the files in ../scripts/tests
//   ./test_comms_framing_vectors --write [dir]        write the files (default ../scripts/tests)
//
// The serial console cases split a byte stream as SerialConsole does: bytes >= 0x80 are OverAscii-encoded
// protocol bytes (decoded, then passed to HDLC), other bytes are console text.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "testhdr.h"
#include "MiniHDLC.h"
#include "ProtocolOverAscii.h"

typedef std::vector<uint8_t> Bytes;

static const uint32_t DEFAULT_MAX_LEN = 5000;

static std::string hex(const Bytes& b)
{
    std::string s;
    char buf[3];
    for (uint8_t v : b)
    {
        snprintf(buf, sizeof(buf), "%02x", v);
        s += buf;
    }
    return s;
}

static Bytes str(const char* s, bool nulTerm = false)
{
    Bytes b((const uint8_t*)s, (const uint8_t*)s + strlen(s));
    if (nulTerm)
        b.push_back(0);
    return b;
}

static Bytes cat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

static std::string jsonStr(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += (char)c;
        }
        else if (c < 0x20 || c >= 0x7f)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else
            out += (char)c;
    }
    return out + "\"";
}

// RICSerial frame payload: msgNum, (msgType << 6) | protocol, then the RICREST element
static Bytes ricrestURL(uint8_t msgNum, const char* url)
{
    return cat({{msgNum, 0x02, 0x00}, str(url, true)});
}

///////////////////////////////////////////////////////////////////////////////
// HDLC using the firmware code
///////////////////////////////////////////////////////////////////////////////

static Bytes hdlcEncode(const Bytes& payload)
{
    MiniHDLC hdlc(nullptr, nullptr, 0xE7, 0xD7, DEFAULT_MAX_LEN, DEFAULT_MAX_LEN);
    Bytes encoded(hdlc.calcEncodedLen(payload.data(), payload.size()));
    uint32_t len = hdlc.encodeFrame(encoded.data(), encoded.size(), payload.data(), payload.size());
    encoded.resize(len);
    return encoded;
}

static std::vector<Bytes> hdlcDecode(const Bytes& stream, uint32_t maxLen)
{
    std::vector<Bytes> frames;
    MiniHDLC hdlc(nullptr, [&](const uint8_t* pFrame, unsigned len) { frames.emplace_back(pFrame, pFrame + len); },
                0xE7, 0xD7, maxLen, maxLen);
    for (uint8_t b : stream)
        hdlc.handleChar(b);
    return frames;
}

static uint16_t crc16(const Bytes& data)
{
    return MiniHDLC::crcUpdateCCITT(0xffff, data.data(), data.size());
}

// A payload (msg header + URL element) whose CRC has an escaped byte (0xE7 or 0xD7) in the given position
static Bytes payloadWithEscapedCRC(bool highByte)
{
    for (int i = 0; i < 100000; i++)
    {
        std::string url = "v" + std::to_string(i);
        Bytes p = ricrestURL(1, url.c_str());
        uint16_t crc = crc16(p);
        uint8_t b = highByte ? (crc >> 8) : (crc & 0xff);
        if (b == 0xE7 || b == 0xD7)
            return p;
    }
    return {};
}

///////////////////////////////////////////////////////////////////////////////
// OverAscii using the firmware code
///////////////////////////////////////////////////////////////////////////////

static Bytes overAsciiEncode(const Bytes& data)
{
    Bytes out(data.size() * 2 + 1);
    uint32_t len = ProtocolOverAscii::encodeFrame(data.data(), data.size(), out.data(), out.size());
    out.resize(len);
    return out;
}

static Bytes overAsciiDecode(const Bytes& stream)
{
    ProtocolOverAscii dec;
    Bytes out;
    for (uint8_t b : stream)
    {
        int v = dec.decodeByte(b);
        if (v >= 0)
            out.push_back((uint8_t)v);
    }
    return out;
}

// Split a console stream as SerialConsole does
static void consoleSplit(const Bytes& stream, std::string& text, std::vector<Bytes>& frames)
{
    ProtocolOverAscii dec;
    MiniHDLC hdlc(nullptr, [&](const uint8_t* pFrame, unsigned len) { frames.emplace_back(pFrame, pFrame + len); },
                0xE7, 0xD7, DEFAULT_MAX_LEN, DEFAULT_MAX_LEN);
    for (uint8_t b : stream)
    {
        if (b >= 0x80)
        {
            int v = dec.decodeByte(b);
            if (v >= 0)
                hdlc.handleChar((uint8_t)v);
        }
        else
            text += (char)b;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Vector files
///////////////////////////////////////////////////////////////////////////////

static std::string framesJson(const std::vector<Bytes>& frames)
{
    std::string s = "[";
    for (size_t i = 0; i < frames.size(); i++)
        s += (i ? ", \"" : "\"") + hex(frames[i]) + "\"";
    return s + "]";
}

static std::string makeHdlcVectors()
{
    std::string j = "{\n";
    j += "  \"_about\": \"HDLC framing as RICSerial uses it. Made from RaftCore MiniHDLC by linux_unit_tests/test_comms_framing_vectors"
         " --write - do not edit. Frame: flag, escaped(payload, CRC high, CRC low), flag. Bytes equal to the flag or"
         " escape are sent as escape, byte ^ 0x20. CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xffff) of the payload."
         " Decoding: bytes from one flag to the next are a frame if there are at least 2 and the last 2 are the CRC of the"
         " others (bytes before the first flag count as a frame too); otherwise they are dropped. A frame longer than"
         " maxLen (payload + CRC) is discarded at that point and decoding starts again from the next byte.\",\n";
    j += "  \"flag\": \"e7\",\n  \"escape\": \"d7\",\n  \"defaultMaxLen\": " + std::to_string(DEFAULT_MAX_LEN) + ",\n";

    // CRC
    struct { const char* desc; Bytes data; } crcCases[] = {
        { "check value", str("123456789") },
        { "empty", {} },
        { "url v, msgNum 1", ricrestURL(1, "v") },
    };
    j += "  \"crc\": [\n";
    for (size_t i = 0; i < sizeof(crcCases) / sizeof(crcCases[0]); i++)
    {
        char crcHex[8];
        snprintf(crcHex, sizeof(crcHex), "%04x", crc16(crcCases[i].data));
        j += std::string("    {\"desc\": ") + jsonStr(crcCases[i].desc) + ", \"data\": \"" + hex(crcCases[i].data) +
             "\", \"crc\": \"" + crcHex + "\"}" + (i + 1 < sizeof(crcCases) / sizeof(crcCases[0]) ? ",\n" : "\n");
    }
    j += "  ],\n";

    // Encode (and decode back)
    Bytes allBytes(256);
    for (int i = 0; i < 256; i++)
        allBytes[i] = i;
    std::vector<std::pair<std::string, Bytes>> enc = {
        { "url v, msgNum 1 (RICRESTProtocol wiki example)", ricrestURL(1, "v") },
        { "url filelist, msgNum 1 (wiki example)", ricrestURL(1, "filelist") },
        { "url v, msgNum 0xe7: flag byte escaped (wiki example)", ricrestURL(0xE7, "v") },
        { "escape byte in payload", {0x01, 0x02, 0xD7, 0x00} },
        { "flag and escape bytes together", {0xE7, 0xD7, 0xE7, 0xD7} },
        { "CRC high byte escaped", payloadWithEscapedCRC(true) },
        { "CRC low byte escaped", payloadWithEscapedCRC(false) },
        { "empty payload", {} },
        { "command frame (cmdName JSON)", cat({{0x05, 0x02, 0x03}, str("{\"cmdName\":\"ufEnd\",\"streamID\":1}", true)}) },
        { "response (msgType 1 = 0x42)", cat({{0x05, 0x42, 0x01}, str("{\"req\":\"v\",\"rslt\":\"ok\"}", true)}) },
        { "publish (msgType 2, protocol 0 = 0x80) with devbin envelope", {0x00, 0x80, 0xDB, 0x03, 0x07, 0x00, 0x08, 0x81, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x05, 0x01} },
        { "file block, stream 1, position 0x0102e7", {0x09, 0x02, 0x04, 0x01, 0x01, 0x02, 0xE7, 0xAA, 0xD7, 0x55} },
        { "every byte value", allBytes },
        { "every byte and both CRC bytes escaped (longest encoding: 2 x payload + 6)", {0xD7, 0xD7, 0xD7, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7, 0xD7, 0xE7, 0xD7} },
    };
    j += "  \"encode\": [\n";
    for (size_t i = 0; i < enc.size(); i++)
    {
        Bytes encoded = hdlcEncode(enc[i].second);
        std::vector<Bytes> back = hdlcDecode(encoded, DEFAULT_MAX_LEN);
        TEST_ASSERT(back.size() == 1 && back[0] == enc[i].second, ("decodes back: " + enc[i].first).c_str());
        j += "    {\"desc\": " + jsonStr(enc[i].first) + ", \"payload\": \"" + hex(enc[i].second) +
             "\", \"encoded\": \"" + hex(encoded) + "\"}" + (i + 1 < enc.size() ? ",\n" : "\n");
    }
    j += "  ],\n";

    // Decode streams
    Bytes fA = hdlcEncode(ricrestURL(1, "v"));
    Bytes fB = hdlcEncode(ricrestURL(2, "filelist"));
    Bytes fC = hdlcEncode({0x00, 0x80, 0x7B, 0x7D});
    Bytes fANoStart(fA.begin() + 1, fA.end());
    Bytes fBNoStart(fB.begin() + 1, fB.end());
    Bytes fBadCRC = fA;
    fBadCRC[fBadCRC.size() - 2] ^= 0x01;
    Bytes zeroLenWithCRC = {0xE7, 0xFF, 0xFF, 0xE7};
    Bytes longPayload(40, 0x11);
    Bytes fLong = hdlcEncode(longPayload);
    Bytes escAtEnd = {0xE7, 0x01, 0xD7, 0xE7};
    std::vector<std::pair<std::string, std::pair<Bytes, uint32_t>>> dec = {
        { "one frame", { fA, DEFAULT_MAX_LEN } },
        { "two frames, each with both flags", { cat({fA, fB}), DEFAULT_MAX_LEN } },
        { "three frames sharing flags", { cat({fA, fBNoStart, Bytes(fC.begin() + 1, fC.end())}), DEFAULT_MAX_LEN } },
        { "no opening flag: bytes before the first flag are a frame", { fANoStart, DEFAULT_MAX_LEN } },
        { "junk before the first flag is dropped (CRC fails)", { cat({str("junk"), fA}), DEFAULT_MAX_LEN } },
        { "bad CRC dropped, next frame decoded", { cat({fBadCRC, fB}), DEFAULT_MAX_LEN } },
        { "repeated flags between frames", { cat({{0xE7, 0xE7, 0xE7}, fA, {0xE7, 0xE7}, fB}), DEFAULT_MAX_LEN } },
        { "flag pairs alone: nothing", { {0xE7, 0xE7, 0xE7, 0xE7}, DEFAULT_MAX_LEN } },
        { "single byte between flags: nothing", { {0xE7, 0x01, 0xE7}, DEFAULT_MAX_LEN } },
        { "2 bytes equal to the CRC of nothing: an empty frame", { zeroLenWithCRC, DEFAULT_MAX_LEN } },
        { "escape just before the flag is dropped with the frame", { cat({escAtEnd, fB}), DEFAULT_MAX_LEN } },
        { "incomplete frame at the end: nothing yet", { Bytes(fA.begin(), fA.end() - 1), DEFAULT_MAX_LEN } },
        { "frame over maxLen dropped, next frame decoded", { cat({fLong, fA}), 20 } },
        { "frame of exactly maxLen (payload + CRC) decoded", { hdlcEncode(Bytes(18, 0x22)), 20 } },
    };
    j += "  \"decode\": [\n";
    for (size_t i = 0; i < dec.size(); i++)
    {
        std::vector<Bytes> frames = hdlcDecode(dec[i].second.first, dec[i].second.second);
        j += "    {\"desc\": " + jsonStr(dec[i].first) + ", \"stream\": \"" + hex(dec[i].second.first) + "\", ";
        if (dec[i].second.second != DEFAULT_MAX_LEN)
            j += "\"maxLen\": " + std::to_string(dec[i].second.second) + ", ";
        j += "\"frames\": " + framesJson(frames) + "}" + (i + 1 < dec.size() ? ",\n" : "\n");
    }
    j += "  ]\n}\n";

    // Checks of the documented behaviour (the vectors record whatever the code does - these make sure it is right)
    TEST_ASSERT(crc16(str("123456789")) == 0x29B1, "CRC check value 0x29b1");
    TEST_ASSERT(hex(hdlcEncode(ricrestURL(1, "v"))) == "e70102007600f4cae7", "wiki example v");
    TEST_ASSERT(hex(hdlcEncode(ricrestURL(1, "filelist"))) == "e701020066696c656c69737400d1dce7", "wiki example filelist");
    TEST_ASSERT(hex(hdlcEncode(ricrestURL(0xE7, "v"))) == "e7d7c7020076000243e7", "wiki example v, msgNum 0xe7");
    TEST_ASSERT(hdlcDecode(cat({fA, fB}), DEFAULT_MAX_LEN).size() == 2, "two frames");
    TEST_ASSERT(hdlcDecode(cat({fBadCRC, fB}), DEFAULT_MAX_LEN).size() == 1, "bad CRC dropped");
    return j;
}

static std::string makeOverAsciiVectors()
{
    std::string j = "{\n";
    j += "  \"_about\": \"OverAscii encoding used for protocol bytes on the serial console (SerialConsole), so every byte"
         " sent has its top bit set and can be told apart from console text. Made from RaftCore ProtocolOverAscii by"
         " linux_unit_tests/test_comms_framing_vectors --write - do not edit. Encoding: 0x00-0x0f -> 0x85, (b ^ 0x20) | 0x80;"
         " 0x10-0x7f -> b | 0x80; 0x80-0x8f -> 0x8e, b ^ 0x20; 0x90-0xff -> 0x8f, b. The bytes sent over the console are"
         " OverAscii(HDLC(frame)). console cases split a received stream as SerialConsole does: bytes >= 0x80 are decoded"
         " and passed to HDLC, other bytes are console text.\",\n";

    Bytes allBytes(256);
    for (int i = 0; i < 256; i++)
        allBytes[i] = i;
    std::vector<std::pair<std::string, Bytes>> enc = {
        { "every byte value", allBytes },
        { "HDLC frame: url v, msgNum 1", hdlcEncode(ricrestURL(1, "v")) },
        { "empty", {} },
    };
    j += "  \"encode\": [\n";
    for (size_t i = 0; i < enc.size(); i++)
    {
        Bytes encoded = overAsciiEncode(enc[i].second);
        TEST_ASSERT(overAsciiDecode(encoded) == enc[i].second, ("decodes back: " + enc[i].first).c_str());
        bool allHigh = true;
        for (uint8_t b : encoded)
            allHigh = allHigh && (b >= 0x80);
        TEST_ASSERT(allHigh, ("every encoded byte has the top bit set: " + enc[i].first).c_str());
        j += "    {\"desc\": " + jsonStr(enc[i].first) + ", \"data\": \"" + hex(enc[i].second) +
             "\", \"encoded\": \"" + hex(encoded) + "\"}" + (i + 1 < enc.size() ? ",\n" : "\n");
    }
    j += "  ],\n";

    // Console streams: text and protocol bytes interleaved
    Bytes fA = overAsciiEncode(hdlcEncode(ricrestURL(1, "v")));
    Bytes fB = overAsciiEncode(hdlcEncode(cat({{0x01, 0x42, 0x01}, str("{\"req\":\"v\",\"rslt\":\"ok\"}", true)})));
    size_t half = fB.size() / 2;
    if (fB[half - 1] == 0x85 || fB[half - 1] == 0x8E || fB[half - 1] == 0x8F)
        half++;
    Bytes fBEscSplit;
    size_t escPos = 0;
    for (size_t i = 0; i < fB.size(); i++)
        if (fB[i] == 0x85 || fB[i] == 0x8E || fB[i] == 0x8F) { escPos = i; break; }
    std::vector<std::pair<std::string, Bytes>> con = {
        { "text only", str("I (1234) SysMan: started\r\n") },
        { "frame only", fA },
        { "log line then frame then text", cat({str("I (10) Log: hello\r\n"), fA, str("typed\r\n")}) },
        { "log text in the middle of a frame", cat({Bytes(fB.begin(), fB.begin() + half), str("W (11) mid\r\n"), Bytes(fB.begin() + half, fB.end())}) },
        { "log text between an escape and its byte", cat({Bytes(fB.begin(), fB.begin() + escPos + 1), str("txt"), Bytes(fB.begin() + escPos + 1, fB.end())}) },
        { "two frames back to back", cat({fA, fB}) },
    };
    j += "  \"console\": [\n";
    for (size_t i = 0; i < con.size(); i++)
    {
        std::string text;
        std::vector<Bytes> frames;
        consoleSplit(con[i].second, text, frames);
        j += "    {\"desc\": " + jsonStr(con[i].first) + ", \"stream\": \"" + hex(con[i].second) + "\", \"text\": " +
             jsonStr(text) + ", \"frames\": " + framesJson(frames) + "}" + (i + 1 < con.size() ? ",\n" : "\n");
    }
    j += "  ]\n}\n";
    return j;
}

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path);
    if (!f.good())
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int main(int argc, char** argv)
{
    bool write = (argc > 1) && (strcmp(argv[1], "--write") == 0);
    std::string dir = write ? (argc > 2 ? argv[2] : "../scripts/tests") : (argc > 1 ? argv[1] : "../scripts/tests");
    struct { const char* name; std::string content; } files[] = {
        { "hdlc_vectors.json", makeHdlcVectors() },
        { "overascii_vectors.json", makeOverAsciiVectors() },
    };
    for (auto& f : files)
    {
        std::string path = dir + "/" + f.name;
        if (write)
        {
            std::ofstream o(path);
            o << f.content;
            printf("Wrote %s\n", path.c_str());
            continue;
        }
        std::string existing;
        TEST_ASSERT(readFile(path, existing), (std::string("read ") + path).c_str());
        TEST_ASSERT(existing == f.content, (path + " matches the firmware code (regenerate with --write if the change"
                    " is intended)").c_str());
    }
    return testSummary("test_comms_framing_vectors");
}
