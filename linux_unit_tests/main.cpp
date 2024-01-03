#include <stdio.h>
#include "utils.h"
#include "ArduinoWString.h"
#include "RaftJson.h"

#include "JSON_test_data_large.h"
#include "JSON_test_data_small.h"

int main()
{
    int constsAxis = RaftJson::getLong(JSON_test_data_small, "consts/axis", 0, "");
    int minotaur = RaftJson::getLong(JSON_test_data_small, "consts/oxis/coo[3]/minotaur[2]", 0, "");
    int comarr = RaftJson::getLong(JSON_test_data_small, "consts/comarr[4]", 0, "");

    int maxQ = RaftJson::getLong(JSON_test_data_large, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0, "");

    printf("Parse ConstsAxis %d minotaur %d maxQ %d comarr %d\n", constsAxis, minotaur, maxQ, comarr);

    const char* testJSON = 
            R"({                                        )"
            R"( "consts": {                             )"
            R"( 	"axis": "1",                        )"
            R"( 	"oxis": {                           )"
            R"( 		"coo": ["pig", 4, "dog", {      )"
            R"( 			"minotaur": [1, 3, 4],      )"
            R"( 			"combine": "aaargh",        )"
            R"( 			"slippery": {               )"
            R"( 				"animal": "goat",       )"
            R"( 				"nice": {},             )"
            R"( 				"polish": "shoes"       )"
            R"( 			},                          )"
            R"( 			"foo": "bar"                )"
            R"( 		}]                              )"
            R"( 	},                                  )"
            R"( 	"exis": "banana",                   )"
            R"( 	"comarr": [6, 5, 4, 3, 3,           )"
            R"( 		{"fish": "stew"}                )"
            R"( 	],                                  )"
            R"( 	"lastly": "elephant"    ,           )"
            R"( 	"bool1":false    ,                  )"
            R"( 	"bool2": 	  true                  )"
            R"( }                                       )"
            R"(}                                        )";

    // Test the getString function
    struct TestElem
    {
        const char* dataPath;
        const char* expStr;
    };
    TestElem findKeyTests [] = {
        { "consts/axis", "1" },
        { "consts/oxis/coo[2]", "dog" },
        { "consts/oxis/coo[3]/minotaur", "[1, 3, 4]" },
        { "consts/oxis/coo[3]/combine", "aaargh" },
        { "consts/oxis/coo[3]/slippery/nice", "{}" },
        { "consts/oxis/coo[3]/foo", "bar" },
        { "consts/exis", "banana" },
        { "consts/comarr/[0]", "6" },
        { "consts/comarr/[4]", "3" },
        { "consts/comarr/[5]/fish", "stew" },
        { "consts/lastly", "elephant" },
        { "consts/bool1", "false" },
        { "consts/bool2", "true" },
    };
    bool anyFailed = false;
    for (int testIdx = 0; testIdx < sizeof(findKeyTests)/sizeof(findKeyTests[0]); testIdx++)
    {
        String keyStartStr = "testGetString testkeyIdx=" + String(testIdx);
        String val = RaftJson::getString(testJSON, findKeyTests[testIdx].dataPath, "");
        if (val != findKeyTests[testIdx].expStr)
        {
            printf("testGetString failed %s <<<%s>>> != <<<%s>>>\n", findKeyTests[testIdx].dataPath, val.c_str(), findKeyTests[testIdx].expStr);
            anyFailed = true;
        }
    }
    if (!anyFailed)
        printf("testGetString all tests passed\n");


}
