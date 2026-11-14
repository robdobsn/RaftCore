/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RestAPIEndpointManager request parsing tests
//
// unencodeHTTPChars must decode any %XX escape exactly once: it previously replaced a fixed list of printable
// escapes one after another, so %27 became ^, "%2526" became "&" (decoded twice), and %0D, %0A, %40 and UTF-8
// were not decoded at all - which broke MicroPython REPL input sent with encodeURIComponent(). Query values from
// getParamsAndNameValues are decoded the same way and are still whitespace-trimmed.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "RestAPIEndpointManager.h"

static void testUnencode()
{
    struct { const char* in; const char* out; } cases[] = {
        { "hello%20world", "hello world" },
        { "c%2B%2B", "c++" },
        { "%27quoted%27", "'quoted'" },
        { "%2526", "%26" },
        { "a%0Db%0Ac%09d", "a\rb\nc\td" },
        { "%03", "\x03" },
        { "%40decorator", "@decorator" },
        { "caf%C3%A9", "caf\xC3\xA9" },
        { "%2f%2F", "//" },
        { "50%", "50%" },
        { "50%zz", "50%zz" },
        { "%4", "%4" },
        { "%", "%" },
        { "x%00y", "x%00y" },
        { "plain", "plain" },
        { "", "" },
    };
    for (auto& c : cases)
    {
        String s = c.in;
        String r = RestAPIEndpointManager::unencodeHTTPChars(s);
        String name = String("unencode \"") + c.in + "\"";
        TEST_ASSERT(strcmp(r.c_str(), c.out) == 0, name.c_str());
        TEST_ASSERT(strcmp(s.c_str(), c.out) == 0, (name + " (in place)").c_str());
    }
}

static void testQueryValues()
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(
        "/upy/repl/input?data=print('a%20%26%20b%3F')%0D&newline=0", params, nameValues);
    TEST_ASSERT(params.size() == 3, "three path params");
    TEST_ASSERT(nameValues.size() == 2, "two name/values");
    if (nameValues.size() == 2)
    {
        // Trimming of values is unchanged: the trailing \r is removed
        TEST_ASSERT(nameValues[0].value == "print('a & b?')", "data decoded once and trimmed");
        TEST_ASSERT(nameValues[1].value == "0", "newline value");
    }
    params.clear();
    nameValues.clear();
    RestAPIEndpointManager::getParamsAndNameValues("/files/my%20dir%2Fa.txt", params, nameValues);
    TEST_ASSERT(params.size() == 2 && params[1] == "my dir/a.txt", "path segment decoded after splitting");
}

int main()
{
    testUnencode();
    testQueryValues();
    return testSummary("test_rest_api_params");
}
