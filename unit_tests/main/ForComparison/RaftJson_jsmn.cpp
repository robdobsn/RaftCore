/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// JSON field extraction
// Many of the methods here support a pDataPath parameter. This uses a syntax like a much simplified XPath:
// [0] returns the 0th element of an array
// / is a separator of nodes
//
// Rob Dobson 2017-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftJson_jsmn.h"
#include "RaftUtils.h"

static const char *MODULE_PREFIX = "RaftJson_jsmn";

#define WARN_ON_PARSE_FAILURE
#define WARN_ON_INVALID_ARGS
// #define DEBUG_PARSE_FAILURE
// #define DEBUG_GET_VALUES
// #define DEBUG_GET_ELEMENT
// #define DEBUG_GET_KEYS
// #define DEBUG_GET_ARRAY_ELEMS
// #define DEBUG_EXTRACT_NAME_VALUES
// #define DEBUG_IS_BOOLEAN
// Note that the following #defines can define a pDataPath in inverted commas to restrict debugging to a specfic key/path
// e.g. #define DEBUG_FIND_KEY_IN_JSON "my/path/to/a/key"
// #define DEBUG_FIND_KEY_IN_JSON "consts/comarr/[1]"
// #define DEBUG_FIND_KEY_IN_JSON_ARRAY
// #define DEBUG_FIND_KEY_IN_JSON_DUMP_TOKENS
// #define DEBUG_PATH_PREFIX
// #define DEBUG_EXTRACT_PATH_PARTS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftJson_jsmn::RaftJson_jsmn(const char *pJsonDoc, bool makeCopy, bool cacheParseResults, int16_t maxTokens)
{
    // Setup the JSON document
    _docAndCache.setJsonDoc(pJsonDoc, makeCopy);

    // Store parse parameters
    _docAndCache.setParseParams(cacheParseResults, maxTokens);
}

RaftJson_jsmn::RaftJson_jsmn(const String& jsonStr, bool cacheParseResults, int16_t maxTokens)
{
    // Setup the JSON document
    _docAndCache.setJsonDoc(jsonStr.c_str(), true);

    // Store parse parameters
    _docAndCache.setParseParams(cacheParseResults, maxTokens);
}

RaftJson_jsmn::~RaftJson_jsmn()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Assignment from string types
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftJson_jsmn& RaftJson_jsmn::operator=(const char *pJsonDoc)
{
    _docAndCache.setJsonDoc(pJsonDoc, true);
    return *this;
}

RaftJson_jsmn& RaftJson_jsmn::operator=(const String& jsonStr)
{
    _docAndCache.setJsonDoc(jsonStr.c_str(), true);
    return *this;
}

RaftJson_jsmn& RaftJson_jsmn::operator=(const std::string& jsonStr)
{
    _docAndCache.setJsonDoc(jsonStr.c_str(), true);
    return *this;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getString
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftJson_jsmn::getString(const char* pJsonDoc,
            const char *pDataPath, const char *defaultValue,
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                nullptr,
                nullptr,
                pJsonDoc,
                pDocAndCache))
        return defaultValue ? defaultValue : "";

    // Extract string
    const char* pStr = pJsonDoc ? pJsonDoc + startPos : pDocAndCache->getJsonDoc();
    return String(pStr, strLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getDouble
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double RaftJson_jsmn::getDouble(const char* pJsonDoc,
            const char *pDataPath, double defaultValue,
            const char* pPathPrefix, 
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                nullptr,
                nullptr,
                pJsonDoc,
                pDocAndCache))
        return defaultValue;
    // Check for booleans
    int retValue = 0;
    const char* pStr = pJsonDoc ? pJsonDoc + startPos : pDocAndCache->getJsonDoc();
    if (RaftJson_jsmn::isBoolean(pStr+startPos, strLen, retValue))
        return retValue;
    return strtod(pStr + startPos, NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getLong
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

long RaftJson_jsmn::getLong(const char* pJsonDoc,
            const char *pDataPath, long defaultValue,
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                nullptr,
                nullptr,
                pJsonDoc,
                pDocAndCache))
        return defaultValue;

    // Check for booleans
    int retValue = 0;
    const char* pStr = pJsonDoc ? pJsonDoc : pDocAndCache->getJsonDoc();
    if (RaftJson_jsmn::isBoolean(pStr+startPos, strLen, retValue))
        return retValue;
    return strtol(pStr + startPos, NULL, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getBool
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::getBool(const char* pJsonDoc,
            const char *pDataPath, bool defaultValue,
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    return RaftJson_jsmn::getLong(pJsonDoc, pDataPath, defaultValue, pPathPrefix, pDocAndCache) != 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getArrayElems
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::getArrayElems(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& strList,
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                nullptr,
                &strList,
                pJsonDoc,
                pDocAndCache))
        return false;
    return elemType == JSMN_ARRAY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getKeys
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::getKeys(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& keysVector,
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                &keysVector,
                nullptr,
                pJsonDoc,
                pDocAndCache))
        return false;
    return elemType == JSMN_OBJECT;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getType
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

jsmntype_t RaftJson_jsmn::getType(const char* pJsonDoc,
            const char* pDataPath, int &arrayLen, 
            const char* pPathPrefix,
            const JSONDocAndCache* pDocAndCache)
{
    // Find the element in the JSON using the pDataPath
    int startPos = 0, strLen = 0;
    jsmntype_t elemType = JSMN_UNDEFINED;
    int elemSize = 0;
    if ((!pJsonDoc && !pDocAndCache) || 
         !getElement(pDataPath, pPathPrefix, startPos, strLen, elemType, elemSize, 
                nullptr,
                nullptr,
                pJsonDoc,
                pDocAndCache))
        return JSMN_UNDEFINED;

    // Check for array
    if (elemType == JSMN_ARRAY)
        arrayLen = elemSize;
    else
        arrayLen = 0;
    return elemType;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getElement
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::getElement(const char *pDataPath,
                        const char* pPathPrefix,
                        int &startPos, int &strLen,
                        jsmntype_t &elemType, int &elemSize,
                        std::vector<String>* pKeysVector,
                        std::vector<String>* pArrayElems,
                        const char *pSourceStr,
                        const JSONDocAndCache* pDocAndCache)
{
    // Get parsed tokens from cache or by parsing the JSON document
    int numTokens = 0;
    jsmntok_t *pTokens = nullptr;
    bool parseResultRequiresDeletion = false;
    const char* pJsonDoc = nullptr;
    if (pDocAndCache)
    {
        if (pDocAndCache->pCachedParseResult)
        {
            pTokens = pDocAndCache->pCachedParseResult;
            numTokens = pDocAndCache->cachedParseNumTokens;
        }
        else
        {
            // Parse json into tokens
            pTokens = parseJson(pDocAndCache->getJsonDoc(), numTokens, pDocAndCache->maxTokens);
            if (pDocAndCache->cacheParseResults)
            {
                pDocAndCache->pCachedParseResult = pTokens;
                pDocAndCache->cachedParseNumTokens = numTokens;
            }
            else
            {
                parseResultRequiresDeletion = true;
            }
        }
        pJsonDoc = pDocAndCache->getJsonDoc();
    }
    else if (pSourceStr)
    {
        // Parse json into tokens
        pTokens = parseJson(pSourceStr, numTokens, RAFT_JSON_MAX_TOKENS);
        parseResultRequiresDeletion = true;
        pJsonDoc = pSourceStr;
    }
    else
    {
#ifdef DEBUG_GET_ELEMENT
        LOG_I(MODULE_PREFIX, "getElement failed null sources %s", dataPathStr.c_str());
#endif
        return false;
    }

    // Check valid parse info
    if (pTokens == NULL)
    {
#ifdef DEBUG_GET_ELEMENT
        LOG_I(MODULE_PREFIX, "getElement no tokens detected in JSON %s", dataPathStr.c_str());
#endif
        if (parseResultRequiresDeletion)
            delete[] pTokens;
        return false;
    }

    // Find token
    int endTokenIdx = 0;
    int startTokenIdx = findKeyInJson(pJsonDoc, pDataPath, pPathPrefix, pTokens, numTokens, endTokenIdx);
    if (startTokenIdx >= 0)
    {
        // Extract information on element
        elemType = pTokens[startTokenIdx].type;
        elemSize = pTokens[startTokenIdx].size;
        startPos = pTokens[startTokenIdx].start;
        strLen = pTokens[startTokenIdx].end - startPos;

        // Check if we should extract the keys of the object
        if (pKeysVector)
        {
            if (elemType == JSMN_OBJECT)
            {
                // Number of keys is the size of the object
                int numKeys = pTokens[startTokenIdx].size;
                pKeysVector->resize(numKeys);
                unsigned int tokIdx = startTokenIdx+1;
                for (int keyIdx = 0; keyIdx < numKeys; keyIdx++)
                {
                    // Check valid
                    if ((tokIdx >= numTokens) || (pTokens[tokIdx].type != JSMN_STRING))
                        break;

                    // Extract the string
                    (*pKeysVector)[keyIdx] = String(pJsonDoc+pTokens[tokIdx].start, pTokens[tokIdx].end-pTokens[tokIdx].start);
                    
                    // Find end of value
#ifdef DEBUG_GET_KEYS
                    LOG_I(MODULE_PREFIX, "getKeys Looking for end of tokIdx %d", tokIdx);
#endif
                    tokIdx = findElemEnd(pJsonDoc, pTokens, numTokens, tokIdx+1);
#ifdef DEBUG_GET_KEYS
                    LOG_I(MODULE_PREFIX, "getKeys ............. Found end at tokIdx %d", tokIdx);
#endif
                }
            }
            else
            {
                pKeysVector->clear();
            }
        }

        // Check if we should extract array elements
        if (pArrayElems)
        {
            if (elemType == JSMN_ARRAY)
            {
                // Number of elements is the size of the array
                int numElems = pTokens[startTokenIdx].size;
                pArrayElems->resize(numElems);
                unsigned int tokIdx = startTokenIdx+1;
                for (int elemIdx = 0; elemIdx < numElems; elemIdx++)
                {
                    // Check valid
                    if (tokIdx >= numTokens)
                        break;

                    // Extract the elem
                    (*pArrayElems)[elemIdx] = String(pJsonDoc+pTokens[tokIdx].start, pTokens[tokIdx].end-pTokens[tokIdx].start);
                    
                    // Find end of elem
                    tokIdx = findElemEnd(pJsonDoc, pTokens, numTokens, tokIdx);
                }
            }
            else
            {
                pArrayElems->clear();
            }
        }        
    }
    else
    {
#ifdef DEBUG_GET_ELEMENT
        LOG_I(MODULE_PREFIX, "getElement failed findKeyInJson %s", dataPathStr.c_str());
#endif
    }

    // Check if we should delete the parse result or leave it for the caller to delete
    if (parseResultRequiresDeletion)
        delete[] pTokens;

    // Return true if found
    return startTokenIdx >= 0;
}

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getString
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getString : Get a string from the JSON
//  * 
//  * @param  {char*} pDataPath       : path of element to return
//  * @param  {char*} defaultValue   : default value to return
//  * @param  {bool&} isValid        : [out] true if element found
//  * @param  {jsmntype_t&} elemType : [out] type of element found (maybe a primitive or maybe object/array)
//  * @param  {int&} elemSize        : [out] size of element found
//  * @param  {char*} pSourceStr     : json to search
//  * @return {String}               : found string value or default
//  */
// String RaftJson_jsmn::getString(const char *pDataPath,
//                          const char *defaultValue, bool &isValid,
//                          jsmntype_t &elemType, int &elemSize,
//                          const char *pSourceStr)
// {
//     // Find the element in the JSON
//     int startPos = 0, strLen = 0;
//     isValid = getElement(pDataPath, startPos, strLen, elemType, elemSize, pSourceStr);
//     if (!isValid)
//         return defaultValue ? defaultValue : "";

//     // Extract string
//     String outStr;
//     char *pStr = safeStringDup(pSourceStr + startPos, strLen,
//                                !(elemType == JSMN_STRING || elemType == JSMN_PRIMITIVE));
//     if (pStr)
//     {
//         outStr = pStr;
//         delete[] pStr;
//     }

//     // If the underlying element is a string or primitive value return size as length of string
//     if (elemType == JSMN_STRING || elemType == JSMN_PRIMITIVE)
//         elemSize = outStr.length();
//     return outStr;
// }

// /**
//  * getString 
//  * 
//  * @param  {char*} pDataPath     : path of element to return
//  * @param  {char*} defaultValue : default value
//  * @param  {char*} pSourceStr   : json string to search
//  * @param  {bool&} isValid      : [out] true if valid
//  * @return {String}             : returned value or default
//  */
// String RaftJson_jsmn::getString(const char *pDataPath, const char *defaultValue,
//                          const char *pSourceStr, bool &isValid)
// {
//     jsmntype_t elemType = JSMN_UNDEFINED;
//     int elemSize = 0;
//     return getString(pDataPath, defaultValue, isValid, elemType, elemSize,
//                      pSourceStr);
// }

// // Alternate form of getString with fewer parameters
// /**
//  * getString 
//  * 
//  * @param  {char*} pDataPath     : path of element to return
//  * @param  {char*} defaultValue : default value
//  * @param  {char*} pSourceStr   : json string to search
//  * @return {String}             : returned value or default
//  */
// String RaftJson_jsmn::getString(const char *pDataPath, const char *defaultValue,
//                          const char *pSourceStr)
// {
//     bool isValid = false;
//     jsmntype_t elemType = JSMN_UNDEFINED;
//     int elemSize = 0;
//     return getString(pDataPath, defaultValue, isValid, elemType, elemSize,
//                      pSourceStr);
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getDouble
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /**
//  * getDouble 
//  * 
//  * @param  {char*} pDataPath      : path of element to return
//  * @param  {double} defaultValue : default value
//  * @param  {bool&} isValid       : [out] true if valid
//  * @param  {char*} pSourceStr    : json string to search
//  * @return {double}              : returned value or default
//  */
// double RaftJson_jsmn::getDouble(const char *pDataPath,
//                          double defaultValue, bool &isValid,
//                          const char *pSourceStr)
// {
//     // Find the element in the JSON
//     int startPos = 0, strLen = 0;
//     jsmntype_t elemType = JSMN_UNDEFINED;
//     int elemSize = 0;
//     isValid = getElement(pDataPath, startPos, strLen, elemType, elemSize, pSourceStr);
//     if (!isValid)
//         return defaultValue;
//     // Check for booleans
//     int retValue = 0;
//     if (RaftJson_jsmn::isBoolean(pSourceStr+startPos, strLen, retValue))
//         return retValue;
//     return strtod(pSourceStr + startPos, NULL);
// }
// /**
//  * getDouble 
//  * 
//  * @param  {char*} pDataPath      : path of element to return
//  * @param  {double} defaultValue : default value
//  * @param  {char*} pSourceStr    : json string to search
//  * @return {double}              : returned value or default
//  */
// double RaftJson_jsmn::getDouble(const char *pDataPath, double defaultValue,
//                          const char *pSourceStr)
// {
//     bool isValid = false;
//     return getDouble(pDataPath, defaultValue, isValid, pSourceStr);
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getLong
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getLong
//  * 
//  * @param  {char*} pDataPath    : path of element to return
//  * @param  {long} defaultValue : default value
//  * @param  {bool&} isValid     : [out] true if valid
//  * @param  {char*} pSourceStr  : json string to search
//  * @return {long}              : returned value or default
//  */
// long RaftJson_jsmn::getLong(const char *pDataPath,
//                      long defaultValue, bool &isValid,
//                      const char *pSourceStr)
// {
//     // Find the element in the JSON
//     int startPos = 0, strLen = 0;
//     jsmntype_t elemType = JSMN_UNDEFINED;
//     int elemSize = 0;
//     isValid = getElement(pDataPath, startPos, strLen, elemType, elemSize, pSourceStr);
//     if (!isValid)
//         return defaultValue;
//     // Check for booleans
//     int retValue = 0;
//     if (RaftJson_jsmn::isBoolean(pSourceStr+startPos, strLen, retValue))
//         return retValue;
//     return strtol(pSourceStr + startPos, NULL, 0);
// }

// /**
//  * getLong 
//  * 
//  * @param  {char*} pDataPath    : path of element to return
//  * @param  {long} defaultValue : default value
//  * @param  {char*} pSourceStr  : json string to search
//  * @return {long}              : returned value or default
//  */
// long RaftJson_jsmn::getLong(const char *pDataPath, long defaultValue, const char *pSourceStr)
// {
//     bool isValid = false;
//     return getLong(pDataPath, defaultValue, isValid, pSourceStr);
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getBool
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getBool
//  * 
//  * @param  {char*} pDataPath    : path of element to return
//  * @param  {bool} defaultValue : default value
//  * @param  {bool&} isValid     : [out] true if valid
//  * @param  {char*} pSourceStr  : json string to search
//  * @return {bool}              : returned value or default
//  */
// bool RaftJson_jsmn::getBool(const char *pDataPath,
//                      bool defaultValue, bool &isValid,
//                      const char *pSourceStr)
// {
//     return RaftJson_jsmn::getLong(pDataPath, defaultValue, isValid, pSourceStr) != 0;
// }

// /**
//  * getBool
//  * 
//  * @param  {char*} pDataPath    : path of element to return
//  * @param  {bool} defaultValue : default value
//  * @param  {char*} pSourceStr  : json string to search
//  * @return {bool}              : returned value or default
//  */
// bool RaftJson_jsmn::getBool(const char *pDataPath, bool defaultValue, const char *pSourceStr)
// {
//     return RaftJson_jsmn::getLong(pDataPath, defaultValue, pSourceStr) != 0;
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getType of outer element of JSON
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getType of outer element of JSON 
//  * 
//  * @param  {int&} arrayLen    : length of array or object
//  * @param  {char*} pSourceStr : json string to search
//  * @return {jsmntype_t}       : returned value is the type of the object
//  */
// jsmntype_t RaftJson_jsmn::getType(int &arrayLen, const char *pSourceStr)
// {
//     arrayLen = 0;
//     // Check for null
//     if (!pSourceStr)
//         return JSMN_UNDEFINED;

//     // Parse json into tokens
//     int numTokens = 0;
//     jsmntok_t *pTokens = parseJson(pSourceStr, numTokens);
//     if (pTokens == NULL)
//         return JSMN_UNDEFINED;

//     // Get the type of the first token
//     arrayLen = pTokens->size;
//     jsmntype_t typ = pTokens->type;
//     delete pTokens;
//     return typ;
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getKeys
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getKeys 
//  * 
//  * @param  {char*} pDataPath                 : path of element to return
//  * @param  {std::vector<String>} keysVector : keys of object to return
//  * @param  {char*} pSourceStr               : json string to search
//  * @return {bool}                           : true if valid
//  */
// bool RaftJson_jsmn::getKeys(const char *pDataPath, std::vector<String>& keysVector, const char *pSourceStr)
// {
//     // Check for null
//     if (!pSourceStr)
//         return false;

//     // Parse json into tokens
//     int numTokens = 0;
//     jsmntok_t *pTokens = parseJson(pSourceStr, numTokens);
//     if (pTokens == NULL)
//     {
//         return false;
//     }

//     // Debug
//     // debugDumpParseResult(pSourceStr, pTokens, numTokens);

//     // Find token
//     int endTokenIdx = 0;
//     int startTokenIdx = findKeyInJson(pSourceStr, pTokens, numTokens, 
//                         pDataPath, endTokenIdx);
//     if (startTokenIdx < 0)
//     {
//         delete[] pTokens;
//         return false;
//     }

//     //Debug
// #ifdef DEBUG_GET_KEYS
//     LOG_I(MODULE_PREFIX, "Found elem startTok %d endTok %d", startTokenIdx, endTokenIdx);
// #endif

//     // Check its an object
//     if ((pTokens[startTokenIdx].type != JSMN_OBJECT) || (pTokens[startTokenIdx].size > MAX_KEYS_TO_RETURN))
//     {
//         delete[] pTokens;
//         return false;
//     }
//     int numKeys = pTokens[startTokenIdx].size;

//     // Extract the keys of the object
//     keysVector.resize(numKeys);
//     unsigned int tokIdx = startTokenIdx+1;
//     String keyStr;
//     for (int keyIdx = 0; keyIdx < numKeys; keyIdx++)
//     {
//         // Check valid
//         if ((tokIdx >= numTokens) || (pTokens[tokIdx].type != JSMN_STRING))
//             break;

//         // Extract the string
//         Raft::strFromBuffer((uint8_t*)pSourceStr+pTokens[tokIdx].start, pTokens[tokIdx].end-pTokens[tokIdx].start, keyStr);
//         keysVector[keyIdx] = keyStr;
        
//         // Find end of value
// #ifdef DEBUG_GET_KEYS
//         LOG_I(MODULE_PREFIX, "getKeys Looking for end of tokIdx %d", tokIdx);
// #endif
//         tokIdx = findElemEnd(pSourceStr, pTokens, numTokens, tokIdx+1);
// #ifdef DEBUG_GET_KEYS
//         LOG_I(MODULE_PREFIX, "getKeys ............. Found end at tokIdx %d", tokIdx);
// #endif
//     }

//     // Clean up
//     delete[] pTokens;
//     return true;
// }


// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // getArrayElems
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /**
//  * getArrayElems 
//  * 
//  * @param  {char*} pDataPath                 : path of element to return
//  * @param  {std::vector<String>} arrayElems : elems of array to return
//  * @param  {char*} pSourceStr               : json string to search
//  * @return {bool}                           : true if valid
//  */
// bool RaftJson_jsmn::getArrayElems(const char *pDataPath, std::vector<String>& arrayElems, const char *pSourceStr)
// {
//     // Check for null
//     if (!pSourceStr)
//         return false;

//     // Parse json into tokens
//     int numTokens = 0;
//     jsmntok_t *pTokens = parseJson(pSourceStr, numTokens);
//     if (pTokens == NULL)
//     {
//         return false;
//     }

//     // Find token
//     int endTokenIdx = 0;
//     int startTokenIdx = findKeyInJson(pSourceStr, pTokens, numTokens, 
//                         pDataPath, endTokenIdx);
//     if (startTokenIdx < 0)
//     {
//         delete[] pTokens;
//         return false;
//     }

//     //Debug
// #ifdef DEBUG_GET_ARRAY_ELEMS
//     LOG_I(MODULE_PREFIX, "Found elem startTok %d endTok %d", startTokenIdx, endTokenIdx);
// #endif

//     // Check its an array
//     if ((pTokens[startTokenIdx].type != JSMN_ARRAY) || (pTokens[startTokenIdx].size > MAX_KEYS_TO_RETURN))
//     {
//         delete[] pTokens;
//         return false;
//     }
//     int numElems = pTokens[startTokenIdx].size;

//     // Extract the array elements as strings (regardless of type)
//     arrayElems.resize(numElems);
//     unsigned int tokIdx = startTokenIdx+1;
//     String elemStr;
//     for (int elemIdx = 0; elemIdx < numElems; elemIdx++)
//     {
//         // Check valid
//         if (tokIdx >= numTokens)
//             break;

//         // Extract the elem
//         Raft::strFromBuffer((uint8_t*)pSourceStr+pTokens[tokIdx].start, pTokens[tokIdx].end-pTokens[tokIdx].start, elemStr);
//         arrayElems[elemIdx] = elemStr;
        
//         // Find end of elem
//         tokIdx = findElemEnd(pSourceStr, pTokens, numTokens, tokIdx);
//     }

//     // Clean up
//     delete[] pTokens;
//     return true;
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parseJson
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * jsmntok_t* RaftJson_jsmn::parseJson 
 * 
 * @param  {char*} jsonStr  : json string to search
 * @param  {int&} numTokens : [out] number of tokens found
 * @param  {int} maxTokens  : max number of tokens to return
 * @return {jsmntok_t*}     : tokens found - NOTE - pointer must be delete[] by caller
 */
jsmntok_t *RaftJson_jsmn::parseJson(const char *jsonStr, int &numTokens,
                              int maxTokens)
{
    // Check for null source string
    if (jsonStr == NULL)
    {
        LOG_E(MODULE_PREFIX, "parseJson input is NULL");
        return NULL;
    }

    // Find how many tokens in the string
    jsmn_parser parser;
    raft_jsmn_init(&parser);
    int tokenCountRslt = raft_jsmn_parse(&parser, jsonStr, strlen(jsonStr),
                                     NULL, maxTokens);
    if (tokenCountRslt < 0)
    {
#ifdef DEBUG_PARSE_FAILURE
        LOG_I(MODULE_PREFIX, "parseJson result %d maxTokens %d jsonLen %d jsonStr %s", tokenCountRslt, 
                        maxTokens, strlen(jsonStr), jsonStr);
#endif
        // raft_jsmn_logLongStr("RaftJson_jsmn: jsonStr", jsonStr, false);
        return NULL;
    }

    // Allocate space for tokens
    if (tokenCountRslt > maxTokens)
        tokenCountRslt = maxTokens;
    jsmntok_t *pTokens = new jsmntok_t[tokenCountRslt];

    // Parse again
    raft_jsmn_init(&parser);
    tokenCountRslt = raft_jsmn_parse(&parser, jsonStr, strlen(jsonStr),
                                 pTokens, tokenCountRslt);
    if (tokenCountRslt < 0)
    {
#ifdef WARN_ON_PARSE_FAILURE
        LOG_W(MODULE_PREFIX, "parseJson result: %d", tokenCountRslt);
#endif
#ifdef DEBUG_PARSE_FAILURE
        LOG_I(MODULE_PREFIX, "parseJson jsonStr %s numTok %d maxTok %d", jsonStr, numTokens, maxTokens);
#endif
        delete[] pTokens;
        return NULL;
    }
    numTokens = tokenCountRslt;
    return pTokens;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// validateJson
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::validateJson(const char* pSourceStr, int& numTokens)
{
    // Check for null source string
    if (pSourceStr == NULL)
    {
#ifdef DEBUG_PARSE_FAILURE
        LOG_I(MODULE_PREFIX, "validateJson input is NULL");
#endif
        return false;
    }

    // Find how many tokens in the string
    jsmn_parser parser;
    raft_jsmn_init(&parser);
    numTokens = raft_jsmn_parse(&parser, pSourceStr, strlen(pSourceStr),
                                     NULL, RAFT_JSON_MAX_TOKENS);
    if (numTokens < 0)
    {
#ifdef DEBUG_PARSE_FAILURE
        LOG_I(MODULE_PREFIX, "validateJson result %d maxTokens %d jsonLen %d", 
                numTokens, RAFT_JSON_MAX_TOKENS, strlen(pSourceStr));
#endif
        // raft_jsmn_logLongStr("RaftJson_jsmn: jsonStr", pSourceStr, false);
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// findElemEnd
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * findElemEnd 
 * 
 * @param  {char*} jsonOriginal   : json to search
 * @param  {jsmntok_t []} tokens  : tokens from jsmn parser
 * @param  {unsigned} int         : count of tokens from parser
 * @param  {int} startTokenIdx    : token index to start search from
 * @return {int}                  : token index of token after the element
 *                                : OR numTokens+1 if element occupies rest of json
 *                                : OR -1 on error
 */
int RaftJson_jsmn::findElemEnd(const char *jsonOriginal, jsmntok_t tokens[],
                          unsigned int numTokens, int startTokenIdx)
{
    // Check valid
    if ((startTokenIdx < 0) || (startTokenIdx >= numTokens))
        return -1;

    // Check for outer object - parentTokenIdx == -1
    int parentTokIdx = tokens[startTokenIdx].parent;
    if (parentTokIdx == -1)
        return numTokens;

    // Handle simple elements
    switch(tokens[startTokenIdx].type)
    {
        case JSMN_PRIMITIVE:
        case JSMN_STRING:
            return startTokenIdx+1;
        default:
            break;
    }

    // Handle arrays and objects - look for element with parent the same or lower
    for (int srchTokIdx = startTokenIdx+1; srchTokIdx < numTokens; srchTokIdx++)
    {
        if (tokens[srchTokIdx].parent <= parentTokIdx)
        {
            return srchTokIdx;
        }
    }
    return numTokens;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// findArrayElem
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * findArrayElem 
 * 
 * @param  {char*} jsonOriginal  : json to search
 * @param  {jsmntok_t []} tokens : tokens from jsmn parser
 * @param  {unsigned} numTokens  : count of tokens from parser
 * @param  {int} startTokenIdx   : token index to start search from
 * @param  {int} arrayElemIdx    : 
 * @return {int}                 : 
 */
int RaftJson_jsmn::findArrayElem(const char *jsonOriginal, jsmntok_t tokens[],
                          unsigned int numTokens, int startTokenIdx, 
                          int arrayElemIdx)
{
    // Check valid
    if ((startTokenIdx < 0) || (startTokenIdx >= numTokens-1))
        return -1;

    // Check this is an array
    if (tokens[startTokenIdx].type != JSMN_ARRAY)
        return -1;

    // // All top-level array elements have the array token as their parent
    // int parentTokIdx = startTokenIdx;

    // Check index is valid
    if (tokens[startTokenIdx].size <= arrayElemIdx)
        return -1; 

    // Loop over elements
    int elemTokIdx = startTokenIdx + 1;
    for (int i = 0; i  < arrayElemIdx; i++)
    {
        elemTokIdx = findElemEnd(jsonOriginal, tokens, numTokens, elemTokIdx);
    }
    return elemTokIdx;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extract path parts from a path string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::extractPathParts(const char* pDataPath, const char* pPathPrefix, 
            std::vector<String>& pathParts, 
            std::vector<int>& arrayIndices)
{
    // Check for null path
    if (pDataPath == NULL)
        return false;

    // Split path on / characters
    pathParts.clear();
    arrayIndices.clear();
    bool isPrefix = pPathPrefix && (strlen(pPathPrefix) != 0);
    const char* pDataPathPos = isPrefix ? pPathPrefix : pDataPath;
    while (*pDataPathPos)
    {
        // Find next /
        const char *pNextPos = strstr(pDataPathPos, "/");

        // Debug
#ifdef DEBUG_EXTRACT_PATH_PARTS
        String debugPathPart = String(pDataPathPos, pNextPos ? pNextPos-pDataPathPos : strlen(pDataPathPos));
        LOG_I(MODULE_PREFIX, "extractPathParts pathPart %s", debugPathPart.c_str());
#endif

        // Extract part
        if (pNextPos == NULL)
        {
            pathParts.push_back(String(pDataPathPos));
            if (!isPrefix)
                break;
            pDataPathPos = pDataPath;
            isPrefix = false;
        }
        else
        {
            pathParts.push_back(String(pDataPathPos, pNextPos-pDataPathPos));
            pDataPathPos = pNextPos+1;
        }
    }

    // Check if any parts of path are array indices
    for (auto& pathPart : pathParts)
    {
        int indexOfSqBracket = pathPart.indexOf("[");
        if (indexOfSqBracket >= 0)
        {
            // Extract array index
            int arrayIdx = strtol(pathPart.c_str()+indexOfSqBracket+1, NULL, 10);
            if (arrayIdx >= 0)
            {
                arrayIndices.push_back(arrayIdx);
            }
            else
            {
                arrayIndices.push_back(-1);
            }

            // Remove the square brackets
            pathPart.remove(indexOfSqBracket);
        }
        else
        {
            arrayIndices.push_back(-1);
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// findKeyInJson
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * findKeyInJson : find an element in a json document using a search path 
 * 
 * @param  {char*} pJsonDoc         : json document to search
 * @param  {char*} pDataPath        : path of searched for element
 * @param  {char*} pPathPrefix      : prefix to add to path
 * @param  {jsmntok_t []} tokens    : tokens from jsmn parser
 * @param  {unsigned int} numTokens : number of tokens
 * @param  {int} endTokenIdx        : token index to end search
 * @param  {jsmntype_t} keyType     : type of json element to find
 * @return {int}                    : index of found token or -1 if failed
 */
int RaftJson_jsmn::findKeyInJson(const char* pJsonDoc, 
                    const char* pDataPath,
                    const char* pPathPrefix,
                    jsmntok_t tokens[],
                    unsigned int numTokens, 
                    int &endTokenIdx,
                    jsmntype_t keyType)
{
    // Check for null source string
    if (pJsonDoc == NULL)
    {
#ifdef WARN_ON_INVALID_ARGS
        LOG_W(MODULE_PREFIX, "findKeyInJson input is NULL");
#endif
        return -1;
    }

    // Check for null path
    if (pDataPath == NULL)
    {
#ifdef WARN_ON_INVALID_ARGS
        LOG_I(MODULE_PREFIX, "findKeyInJson path is NULL");
#endif
        return -1;
    }

#ifdef DEBUG_FIND_KEY_IN_JSON
    String debugFullPathWithPrefix = pDataPath;
    if (pPathPrefix)
        debugFullPathWithPrefix = String(pPathPrefix) + "/" + debugFullPathWithPrefix;
    String debugTestStr = DEBUG_FIND_KEY_IN_JSON "__";
    bool debugIsTestPath = (debugTestStr.equals(debugFullPathWithPrefix + "__") || debugTestStr == "__");
#endif

#ifdef DEBUG_FIND_KEY_IN_JSON_DUMP_TOKENS
    if (debugIsTestPath)
    {
        int numTokens = 0;
        jsmntok_t* pTok = parseJson(pJsonDoc, numTokens, 10000);
        debugDumpParseResult(pJsonDoc, pTok, numTokens, "foundKeyInJson TOKENS");
        delete[] pTok;
    }
#endif

    // Split path on / characters
    std::vector<String> pathParts;
    std::vector<int> arrayIndices;
    if (!extractPathParts(pDataPath, pPathPrefix, pathParts, arrayIndices))
    {
#ifdef DEBUG_FIND_KEY_IN_JSON
        if (debugIsTestPath)
        {
            LOG_I(MODULE_PREFIX, "findKeyInJson extractPathParts failed");
        }
#endif
        return -1;
    }
    else
    {
#ifdef DEBUG_FIND_KEY_IN_JSON
        if (debugIsTestPath)
        {
            LOG_I(MODULE_PREFIX, "findKeyInJson extractPathParts OK");
            String testStr;
            for (auto& pathPart : pathParts)
            {
                testStr += "<" + pathPart + ">";
            }
            if (testStr.isEmpty())
                testStr = "EMPTY";
            String testStr2;
            for (auto& arrayIdx : arrayIndices)
            {
                testStr2 += String(arrayIdx) + ",";
            }
            if (testStr2.isEmpty())
                testStr2 = "EMPTY";
            LOG_I(MODULE_PREFIX, "findKeyInJson pathParts %s arrayIndices %s", testStr.c_str(), testStr2.c_str());
        }
#endif
    }

    // Find the element in the parsed JSON
    int curTokenIdx = 0;
    int maxTokenIdx = numTokens - 1;

    // Iterate through the path elements finding each one
    for (uint32_t pathPartIdx = 0; pathPartIdx < pathParts.size(); pathPartIdx++)
    {
        // Get path part and see if this is the last part of the path
        const String& pathPart = pathParts[pathPartIdx];
        const bool atLastPathPart = pathPartIdx == pathParts.size()-1;

#ifdef DEBUG_FIND_KEY_IN_JSON
        if (debugIsTestPath)
        {
            LOG_I(MODULE_PREFIX, "findKeyInJson pathPart %s arrayIdx %d", pathPart.c_str(), arrayIndices[pathPartIdx]);
        }
#endif

        // Iterate over tokens to find key of the right type
        // If we are already looking at the last part then search for requested type
        // Otherwise search for an element that will contain the next level key
        jsmntype_t keyTypeToFind = atLastPathPart ? keyType : JSMN_STRING;
        for (int tokIdx = curTokenIdx; tokIdx <= maxTokenIdx;)
        {
            // See if the key matches - this can either be a string match on an object key or
            // just an array element match (with an empty key)
            jsmntok_t *pTok = tokens + tokIdx;
            bool keyMatchFound = false;
            if ((pTok->type == JSMN_STRING) && 
                        (pathPart.length() == pTok->end - pTok->start) && 
                        (strncmp(pJsonDoc + pTok->start, pathPart.c_str(), pTok->end - pTok->start) == 0))
            {
                keyMatchFound = true;
                tokIdx += 1;
                pTok = tokens + tokIdx;
            }
            else if (((pTok->type == JSMN_ARRAY) || (pTok->type == JSMN_OBJECT)) && (pathPart.length() == 0))
            {
                keyMatchFound = true;
            }
#ifdef DEBUG_FIND_KEY_IN_JSON
            if (debugIsTestPath)
            {
                LOG_I(MODULE_PREFIX, "findKeyInJson tokIdx %d Token type %d pathPart %s reqdIdx %d matchFound %d", 
                        tokIdx, pTok->type, pathPart.c_str(), arrayIndices[pathPartIdx], keyMatchFound);
            }
#endif

            if (keyMatchFound)
            {
                // We have found the matching key so now for the contents ...

                // Check if we were looking for an array element
                if (arrayIndices[pathPartIdx] >= 0)
                {
                    if (tokens[tokIdx].type == JSMN_ARRAY)
                    {
                        int newTokIdx = findArrayElem(pJsonDoc, tokens, numTokens, tokIdx, arrayIndices[pathPartIdx]);
#ifdef DEBUG_FIND_KEY_IN_JSON_ARRAY
                        if (debugIsTestPath)
                        {
                            LOG_I(MODULE_PREFIX, "findKeyInJson TokIdxArray inIdx %d, reqdArrayIdx %d, outTokIdx %d", 
                                    tokIdx, arrayIndices[pathPartIdx], newTokIdx);
                        }
#endif
                        tokIdx = newTokIdx;
                    }
                    else
                    {
                        // This isn't an array element
#ifdef DEBUG_FIND_KEY_IN_JSON_ARRAY
                        if (debugIsTestPath)
                        {
                            LOG_I(MODULE_PREFIX, "findKeyInJson NOT AN ARRAY ELEM");
                        }
#endif
                        return -1;
                    }
                }

                // atNodeLevel indicates that we are now at the level of the JSON tree that the user requested
                // - so we should be extracting the value referenced now
                if (atLastPathPart)
                {
                    // LOG_I(MODULE_PREFIX, "findKeyInJson we have got it %d", tokIdx);
                    if ((keyTypeToFind == JSMN_UNDEFINED) || (tokens[tokIdx].type == keyTypeToFind))
                    {
                        endTokenIdx = findElemEnd(pJsonDoc, tokens, numTokens, tokIdx);
                        return tokIdx;
                    }
#ifdef DEBUG_FIND_KEY_IN_JSON
                    if (debugIsTestPath)
                    {
                        LOG_I(MODULE_PREFIX, "findKeyInJson AT NOTE LEVEL FAIL");
                    }
#endif
                    return -1;
                }
                else
                {
                    // Check for an object
#ifdef DEBUG_FIND_KEY_IN_JSON
                    if (debugIsTestPath)
                    {
                        LOG_I(MODULE_PREFIX, "findKeyInJson findElemEnd inside tokIdx %d", tokIdx);
                    }
#endif
                    if ((tokens[tokIdx].type == JSMN_OBJECT) || (tokens[tokIdx].type == JSMN_ARRAY))
                    {
                        // Continue next level of search in this object
                        maxTokenIdx = findElemEnd(pJsonDoc, tokens, numTokens, tokIdx);
                        curTokenIdx = (tokens[tokIdx].type == JSMN_OBJECT) ? tokIdx + 1 : tokIdx;
#ifdef DEBUG_FIND_KEY_IN_JSON
                        if (debugIsTestPath)
                        {
                            LOG_I(MODULE_PREFIX, "findKeyInJson tokIdx %d max %d next %d", 
                                tokIdx, maxTokenIdx, curTokenIdx);
                        }
#endif
                        break;
                    }
                    else
                    {
                        // Found a key in the path but it didn't point to an object so we can't continue
#ifdef DEBUG_FIND_KEY_IN_JSON
                        if (debugIsTestPath)
                        {
                            LOG_I(MODULE_PREFIX, "findKeyInJson FOUND KEY BUT NOT POINTING TO OBJ");
                        }
#endif
                        return -1;
                    }
                }
            }
            else if (pTok->type == JSMN_STRING)
            {
                // We're at a key string but it isn't the one we want so skip its contents
                tokIdx = findElemEnd(pJsonDoc, tokens, numTokens, tokIdx+1);
            }
            else if (pTok->type == JSMN_OBJECT)
            {
                // Move to the first key of the object
                tokIdx++;
            }
            else if (pTok->type == JSMN_ARRAY)
            {
                // Root level array which doesn't match the pDataPath
#ifdef DEBUG_FIND_KEY_IN_JSON
                if (debugIsTestPath)
                {
                    LOG_I(MODULE_PREFIX, "findKeyInJson UNEXPECTED ARRAY");
                }
#endif
                return -1;
            }
            else
            {
                // Shouldn't really get here as all keys are strings
                tokIdx++;
            }
        }
    }
#ifdef DEBUG_FIND_KEY_IN_JSON
    if (debugIsTestPath)
    {
        LOG_I(MODULE_PREFIX, "findKeyInJson DROPPED OUT");
    }
#endif
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * escapeString 
 * 
 * @param  {String} strToEsc : string in which to replace characters which are invalid in JSON
 */
void RaftJson_jsmn::escapeString(String &strToEsc)
{
    // Replace characters which are invalid in JSON
    strToEsc.replace("\\", "\\\\");
    strToEsc.replace("\"", "\\\"");
    strToEsc.replace("\n", "\\n");
}
/**
 * unescapeString 
 * 
 * @param  {String} strToUnEsc : string in which to restore characters which are invalid in JSON
 */
void RaftJson_jsmn::unescapeString(String &strToUnEsc)
{
    // Replace characters which are invalid in JSON
    strToUnEsc.replace("\\\"", "\"");
    strToUnEsc.replace("\\\\", "\\");
    strToUnEsc.replace("\\n", "\n");
}

void RaftJson_jsmn::debugDumpParseResult(const char* pSourceStr, jsmntok_t* pTokens, int numTokens, const char* debugLinePrefix)
{
    LOG_I(MODULE_PREFIX, "%s Idx      Type Size Start  End Parent String", debugLinePrefix);
    for (int i = 0; i < numTokens; i++)
    {
        String elemStr = String((uint8_t*)pSourceStr+pTokens[i].start, pTokens[i].end-pTokens[i].start);
        LOG_I(MODULE_PREFIX, "%s %3d %9s %4d %5d %4d %6d %s",
                debugLinePrefix,
                i, 
                getElemTypeStr(pTokens[i].type), 
                pTokens[i].size, pTokens[i].start, pTokens[i].end,
                pTokens[i].parent, elemStr.c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert name value pairs to JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftJson_jsmn::getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces)
{
    // Calculate length for efficiency
    uint32_t reserveLen = 0;
    for (NameValuePair& pair : nameValuePairs)
        reserveLen += 6 + pair.name.length() + pair.value.length();

    // Generate JSON
    String jsonStr;
    jsonStr.reserve(reserveLen);
    for (NameValuePair& pair : nameValuePairs)
    {
        if (jsonStr.length() > 0)
            jsonStr += ',';
        if (pair.value.startsWith("[") || pair.value.startsWith("{"))
            jsonStr += "\"" + pair.name + "\":" + pair.value;
        else
            jsonStr += "\"" + pair.name + "\":\"" + pair.value + "\"";
    }
    if (includeOuterBraces)
        return "{" + jsonStr + "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert JSON object to HTML query string syntax
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftJson_jsmn::getHTMLQueryFromJSON(const String& jsonStr)
{
    // Get keys of object
    std::vector<String> keyStrs;
    RaftJson_jsmn::getKeys(jsonStr.c_str(), "", keyStrs);
    if (keyStrs.size() == 0)
        return "";

    // Fill object
    String outStr;
    for (String& keyStr : keyStrs)
    {
        String valStr = getString(jsonStr.c_str(), keyStr.c_str(), "");
        if (valStr.length() == 0)
            continue;
        if (outStr.length() != 0)
            outStr += "&";
        outStr += keyStr + "=" + valStr;
    }
    return outStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extract name-value pairs from string
// nameValueSep - e.g. "=" for HTTP
// pairDelim - e.g. "&" for HTTP
// pairDelimAlt - e.g. ";" for HTTP alternate (pass 0 if not needed)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftJson_jsmn::extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson_jsmn::NameValuePair>& nameValuePairs)
{
   // Count the pairs
    uint32_t pairCount = 0;
    const char* pCurSep = inStr.c_str();
    while(pCurSep)
    {
        pCurSep = strstr(pCurSep, pNameValueSep);
        if (pCurSep)
        {
            pairCount++;
            pCurSep++;
        }
    }

#ifdef DEBUG_EXTRACT_NAME_VALUES
    // Debug
    LOG_I(MODULE_PREFIX, "extractNameValues found %d nameValues", pairCount);
#endif

    // Extract the pairs
    nameValuePairs.resize(pairCount);
    pCurSep = inStr.c_str();
    bool sepTypeIsEqualsSign = true;
    uint32_t pairIdx = 0;
    String name, val;
    while(pCurSep)
    {
        // Each pair has the form "name=val;" (semicolon missing on last pair)
        const char* pElemStart = pCurSep;
        if (sepTypeIsEqualsSign)
        {
            // Check for missing =
            pCurSep = strstr(pElemStart, pNameValueSep);
            if (!pCurSep)
                break;
            name = String((uint8_t*)pElemStart, pCurSep-pElemStart);
            pCurSep++;
        }
        else
        {
            // Handle two alternatives - sep or no sep
            pCurSep = strstr(pElemStart, pPairDelim);
            if (!pCurSep && pPairDelimAlt)
                pCurSep = strstr(pElemStart, pPairDelimAlt);
            if (pCurSep)
            {
                val = String((uint8_t*)pElemStart, pCurSep-pElemStart);
                pCurSep++;
            }
            else
            {
                val = pElemStart;
            }
        }

        // Next separator
        sepTypeIsEqualsSign = !sepTypeIsEqualsSign;
        if (!sepTypeIsEqualsSign)
            continue;

        // Store and move on
        if (pairIdx >= pairCount)
            break;
        name.trim();
        val.trim();
        nameValuePairs[pairIdx] = {name,val};
        pairIdx++;
    }

#ifdef DEBUG_EXTRACT_NAME_VALUES
    // Debug
    for (RaftJson_jsmn::NameValuePair& pair : nameValuePairs)
        LOG_I(MODULE_PREFIX, "extractNameValues name %s val %s", pair.name.c_str(), pair.value.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// isBoolean
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::isBoolean(const char* pBuf, uint32_t bufLen, int &retValue)
{
    if ((*pBuf == 'f') || (*pBuf == 't'))
    {
        String elemStr(pBuf, bufLen);
#ifdef DEBUG_IS_BOOLEAN
        LOG_I(MODULE_PREFIX, "isBoolean str %s", elemStr.c_str());
#endif
        if (elemStr.equals("true"))
        {
            retValue = 1;
            return true;
        }
        else if (elemStr.equals("false"))
        {
            retValue = 0;
            return true;
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Re-create JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef RDJSON_RECREATE_JSON
int RaftJson_jsmn::recreateJson(const char *js, jsmntok_t *t,
                         size_t count, int indent, String &outStr)
{
    int i, j, k;

    if (count == 0)
    {
        return 0;
    }
    if (t->type == JSMN_PRIMITIVE)
    {
        LOG_D(MODULE_PREFIX, "recreateJson Found primitive size %d, start %d, end %d",
                  t->size, t->start, t->end);
        LOG_D(MODULE_PREFIX, "recreateJson %.*s", t->end - t->start, js + t->start);
        char *pStr = safeStringDup(js + t->start,
                                   t->end - t->start);
        outStr.concat(pStr);
        delete[] pStr;
        return 1;
    }
    else if (t->type == JSMN_STRING)
    {
        LOG_D(MODULE_PREFIX, "recreateJson Found string size %d, start %d, end %d",
                  t->size, t->start, t->end);
        LOG_D(MODULE_PREFIX, "recreateJson '%.*s'", t->end - t->start, js + t->start);
        char *pStr = safeStringDup(js + t->start,
                                   t->end - t->start);
        outStr.concat("\"");
        outStr.concat(pStr);
        outStr.concat("\"");
        delete[] pStr;
        return 1;
    }
    else if (t->type == JSMN_OBJECT)
    {
        LOG_D(MODULE_PREFIX, "recreateJson Found object size %d, start %d, end %d",
                  t->size, t->start, t->end);
        j = 0;
        outStr.concat("{");
        for (i = 0; i < t->size; i++)
        {
            for (k = 0; k < indent; k++)
            {
                LOG_D(MODULE_PREFIX, "  ");
            }
            j += recreateJson(js, t + 1 + j, count - j, indent + 1, outStr);
            outStr.concat(":");
            LOG_D(MODULE_PREFIX, ": ");
            j += recreateJson(js, t + 1 + j, count - j, indent + 1, outStr);
            LOG_D(MODULE_PREFIX, "");
            if (i != t->size - 1)
            {
                outStr.concat(",");
            }
        }
        outStr.concat("}");
        return j + 1;
    }
    else if (t->type == JSMN_ARRAY)
    {
        LOG_D(MODULE_PREFIX, "#Found array size %d, start %d, end %d",
                  t->size, t->start, t->end);
        j = 0;
        outStr.concat("[");
        LOG_D(MODULE_PREFIX, "");
        for (i = 0; i < t->size; i++)
        {
            for (k = 0; k < indent - 1; k++)
            {
                LOG_D(MODULE_PREFIX, "  ");
            }
            LOG_D(MODULE_PREFIX, "   - ");
            j += recreateJson(js, t + 1 + j, count - j, indent + 1, outStr);
            if (i != t->size - 1)
            {
                outStr.concat(",");
            }
            LOG_D(MODULE_PREFIX, "");
        }
        outStr.concat("]");
        return j + 1;
    }
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Print JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftJson_jsmn::doPrint(const char *jsonStr)
{
    jsmn_parser parser;
    raft_jsmn_init(&parser);
    int tokenCountRslt = raft_jsmn_parse(&parser, jsonStr, strlen(jsonStr),
                                     NULL, 1000);
    if (tokenCountRslt < 0)
    {
        LOG_I(MODULE_PREFIX, "JSON parse result: %d", tokenCountRslt);
        return false;
    }
    jsmntok_t *pTokens = new jsmntok_t[tokenCountRslt];
    raft_jsmn_init(&parser);
    tokenCountRslt = raft_jsmn_parse(&parser, jsonStr, strlen(jsonStr),
                                 pTokens, tokenCountRslt);
    if (tokenCountRslt < 0)
    {
        LOG_I(MODULE_PREFIX, "JSON parse result: %d", tokenCountRslt);
        delete pTokens;
        return false;
    }
    // Top level item must be an object
    if (tokenCountRslt < 1 || pTokens[0].type != JSMN_OBJECT)
    {
        LOG_E(MODULE_PREFIX "JSON must have top level object");
        delete pTokens;
        return false;
    }
    LOG_D(MODULE_PREFIX "Dumping");
    recreateJson(jsonStr, pTokens, parser.toknext, 0);
    delete pTokens;
    return true;
}

#endif // CONFIG_MANAGER_RECREATE_JSON
