
#pragma once

#include "RaftArduino.h"
#include "Logger.h"

String getNextPathElem(const char*& pDataPathPos)
{
    const char* pElemStart = pDataPathPos;
    while (*pDataPathPos && (*pDataPathPos != '/'))
        pDataPathPos++;
    String pathPart = String(pElemStart, pDataPathPos - pElemStart);
    if (*pDataPathPos)
        pDataPathPos++;
    return pathPart;
}

const char* locateStringElement(const char* pJsonDocPos, const char*& pElemStart, const char*& pElemEnd, bool includeQuotes = false)
{
    // Skip quote if at start of string
    if (!includeQuotes && (*pJsonDocPos == '"'))
        pJsonDocPos++;

    // Find end of string
    pElemStart = pJsonDocPos;
    while (*pJsonDocPos && (*pJsonDocPos != '"'))
        pJsonDocPos++;

    // Return string start and end
    if (*pJsonDocPos == '"')
    {
        pElemEnd = includeQuotes ? pJsonDocPos + 1 : pJsonDocPos;
        return pJsonDocPos + 1;
    }
    return nullptr;
}

const char* skipOverElement(const char* pJsonDocPos, const char* pMaxDocPos, 
            const char*& pElemStart, const char*& pElemEnd)
{
    // Skip whitespace, commas and colons
    while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',') || (*pJsonDocPos == ':'))
            && (pJsonDocPos < pMaxDocPos))
        pJsonDocPos++;
    if (!*pJsonDocPos || (pJsonDocPos >= pMaxDocPos))
        return nullptr;
    pElemStart = pJsonDocPos;
        
    // Check for kind of element
    if ((*pJsonDocPos == '{') || (*pJsonDocPos == '['))
    {
        // Find end of object
        char braceChar = *pJsonDocPos;
        int numBraces = 1;
        pJsonDocPos++;
        // Skip to end of object
        bool insideString = false;
        while (*pJsonDocPos && (numBraces > 0) && (pJsonDocPos < pMaxDocPos))
        {
            if (*pJsonDocPos == '"')
                insideString = !insideString;
            if (!insideString)
            {
                if (*pJsonDocPos == braceChar)
                    numBraces++;
                else if (*pJsonDocPos == ((braceChar == '{') ? '}' : ']'))
                    numBraces--;
            }
            pJsonDocPos++;
        }
        if (!*pJsonDocPos)
            return nullptr;
        pElemEnd = pJsonDocPos;
        return pJsonDocPos;
    }
    else if (*pJsonDocPos == '"')
    {
        // Find end of string
        pJsonDocPos++;
        while (*pJsonDocPos && (*pJsonDocPos != '"') && (pJsonDocPos < pMaxDocPos))
            pJsonDocPos++;
        if (!*pJsonDocPos)
            return nullptr;
        pElemEnd = pJsonDocPos;
        pJsonDocPos++;
        return pJsonDocPos;
    }
    else
    {
        // Find end of element
        while (*pJsonDocPos && (*pJsonDocPos != ',') && (pJsonDocPos < pMaxDocPos))
            pJsonDocPos++;
        if (!*pJsonDocPos)
            return nullptr;
        pElemEnd = pJsonDocPos;
        return pJsonDocPos;
    }
}

// const char* locateNthArrayElement(const char* pJsonDocPos, 
//             const char*& pElemStart, const char*& pElemEnd,
//             int arrayIdx)
// {
//     // Skip over elements until we get to the one we want
//     for (int i = 0; i < arrayIdx; i++)
//     {
//         pJsonDocPos = skipOverElement(pJsonDocPos, pElemStart, pElemEnd);
//         if (!pJsonDocPos)
//             return nullptr;
//         // Skip whitespace
//         while (*pJsonDocPos && (*pJsonDocPos <= ' '))
//             pJsonDocPos++;
//         // Check for end of array
//         if (*pJsonDocPos == ']')
//             return nullptr;
//     }
//     return pJsonDocPos;
// }

// Locate value inside element with key
// The key can be empty in which case the entire object is returned
// The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
// The key can be a string in which case the value for that key is returned if the element is an object

const char* locateElementValueWithKey(const char* pJsonDocPos,
            const char* pMaxDocPos,
            const char* pKey,
            const char*& pElemStart, const char*& pElemEnd)
{
    // If key is empty return the entire element
    if (!pKey || !*pKey)
    {
        // Skip to end of element
        pJsonDocPos = skipOverElement(pJsonDocPos, pMaxDocPos, pElemStart, pElemEnd);
        if (!pJsonDocPos)
            return nullptr;
        // Skip whitespace
        while (*pJsonDocPos && (*pJsonDocPos <= ' '))
            pJsonDocPos++;
        return pJsonDocPos;
    }

    // Skip any whitespace
    while (*pJsonDocPos && (*pJsonDocPos <= ' '))
        pJsonDocPos++;
    if ((*pJsonDocPos != '{') && (*pJsonDocPos != '['))
        return nullptr;

    // Check for the type of element - object or array
    bool isObject = true;
    uint32_t arrayIdx = 0;
    uint32_t elemCount = 0;
    if (*pJsonDocPos == '[')
    {
        isObject = false;
        // Check the key is an array index
        if (*pKey != '[')
            return nullptr;
        pKey++;
        // Extract array index from key
        arrayIdx = atoi(pKey);
    }
    pJsonDocPos++;

    // Skip over elements until we get to the one we want
    const char* pKeyStart = pJsonDocPos;
    const char* pKeyEnd = nullptr;
    while (*pJsonDocPos && pJsonDocPos < pMaxDocPos)
    {
        // Check for object - in which case what comes first is the key
        if (isObject)
        {
            // Skip to start of key
            while (*pJsonDocPos && (*pJsonDocPos != '"'))
                pJsonDocPos++;
            if (!*pJsonDocPos)
                return nullptr;
            // Extract key string
            pJsonDocPos = locateStringElement(pJsonDocPos, pKeyStart, pKeyEnd, false);
            if (!pJsonDocPos)
                return nullptr;
            // Skip over any whitespace and colons
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ':')))
                pJsonDocPos++;
            if (!*pJsonDocPos)
                return nullptr;
        }   
        // Skip to end of element
        pJsonDocPos = skipOverElement(pJsonDocPos, pMaxDocPos, pElemStart, pElemEnd);
        if (!pJsonDocPos)
            return nullptr;
        // Skip whitespace
        while (*pJsonDocPos && (*pJsonDocPos <= ' '))
            pJsonDocPos++;
        // Check if this is the key we are looking for
        if (isObject)
        {
            if (strncmp(pKeyStart, pKey, pKeyEnd - pKeyStart) == 0)
                return pJsonDocPos;
            // Check for end of object
            if (*pJsonDocPos == '}')
                return nullptr;
        }
        else
        {
            if (arrayIdx == elemCount)
                return pJsonDocPos;
            elemCount++;
            // Check for end of array
            if (*pJsonDocPos == ']')
                return nullptr;
        }
    }
    return nullptr;
}

// String getCurrentElement(const char*& pJsonDocPos, int arrayIdx, bool& isValid)
// {
//     // Skip any whitespace
//     while (*pJsonDocPos && (*pJsonDocPos <= ' '))
//         pJsonDocPos++;

//     // Check for array
//     if (*pJsonDocPos == '[')
//     {
//         // If we're looking for an array element then return it
//         if (arrayIdx >= 0)
//             return getNthArrayElement(pJsonDocPos+1, arrayIdx, isValid);
//         // Otherwise return failure
//         isValid = false;
//         return "";
//     }

//     // Check for object
//     if (*pJsonDocPos == '{')
//     {
//         // Return entire element
//         return getObjectElement(pJsonDocPos, isValid);
//     }

//     // Check for string
//     if (*pJsonDocPos == '"')
//     {
//         // Return string contents
//         return getStringElement(pJsonDocPos, isValid, true);
//     }

//     // Nothing else is valid
//     isValid = false;
//     return "";
// }

const char* locateElementByPathPart(const char* pJsonDocPos, 
            const char* pMaxDocPos,
            const char* pPathPart, 
            const char*& pElemStart, const char*& pElemEnd)
{
    // Skip any whitespace
    while (*pJsonDocPos && (*pJsonDocPos <= ' '))
        pJsonDocPos++;

    // Locate element
    pJsonDocPos = locateElementValueWithKey(pJsonDocPos, pMaxDocPos, pPathPart, pElemStart, pElemEnd);
    if (!pJsonDocPos)
        return nullptr;
    return pJsonDocPos;

    // // Check for object
    // if (*pJsonDocPos == '{')
    // {
    //     // Get the value for the key or the entire object if the path part is empty
    //     pJsonDocPos = locateObjectValueWithKey(pJsonDocPos, pElemStart, pElemEnd, pPathPart);
    //     if (!pJsonDocPos)
    //         return nullptr;
    //     // Return the position and start and end of value
    //     return pJsonDocPos;
    // }
    // else if (*pJsonDocPos == '[')
    // {
    //     // Get the value for the key
    //     int arrayIdx = atoi(pPathPart);
    //     pJsonDocPos = locateNthArrayElement(pJsonDocPos+1, pElemStart, pElemEnd, arrayIdx);
    //     if (!pJsonDocPos)
    //         return nullptr;
    //     // Find the start and end of value
    //     pJsonDocPos = skipOverElement(pJsonDocPos, pElemStart, pElemEnd);
    //     if (!pJsonDocPos)
    //         return nullptr;
    //     // Return the position and start and end of value
    //     return pJsonDocPos;
    // }
    // return nullptr;
}

String test____getString(const char* pDataPath, const char* defaultValue, const char* pPathPrefix, const char* pJsonDoc)
{
    // Locate the element
    const char* pJsonDocPos = pJsonDoc;
    const char* pElemStart = nullptr;
    const char* pElemEnd = nullptr;
    pJsonDocPos = locateElementByPathPart(pJsonDocPos, pJsonDocPos + strlen(pJsonDoc), pDataPath, pElemStart, pElemEnd);

    // Check if we found the element
    if (!pJsonDocPos)
        return defaultValue;

    // Extract the value
    if (*pElemStart == '"')
    {
        // Skip quote if at start of string
        pElemStart++;
    }
    if (*pElemEnd == '"')
    {
        // Skip quote if at end of string
        pElemEnd--;
    }
    return String(pElemStart, pElemEnd - pElemStart);

    // // The document can be an object or an array
    // const char* pJsonDocPos = pJsonDoc;
    // // Skip any whitespace
    // while (*pJsonDocPos && (*pJsonDocPos <= ' '))
    //     pJsonDocPos++;
    // // Check for object
    // if (*pJsonDocPos == '{')
    // {
    //     // Get the value for the key
    //     const char* pElemStart = nullptr;
    //     const char* pElemEnd = nullptr;
    //     pJsonDocPos = skipToObjectValueForKey(pJsonDocPos+1, pElemStart, pElemEnd, pDataPath);
    //     if (!pJsonDocPos)
    //         return defaultValue;
    //     // Find the start and end of value
    //     pJsonDocPos = skipOverElement(pJsonDocPos, pElemStart, pElemEnd);
    //     if (!pJsonDocPos)
    //         return defaultValue;
    //     // Extract the value
    //     return String(pElemStart, pElemEnd - pElemStart);
    // }
    // return defaultValue;
}

    // const char* pDataPathPos = nullptr;
    // const char* pDataPathPartNext = pDataPath;
    // const char* pJsonDocPos = pJsonDoc;
    // const char* pElemStart = nullptr;
    // int arrayIdx = -1;
    // String pathPart;
    // while (*pJsonDocPos)
    // {
    //     // Get next part of path if needed
    //     if (!pDataPathPos)
    //     {
    //         pathPart = getNextPathElem(pDataPathPartNext);
    //         pDataPathPos = pDataPathPartNext;
    //         if (*pDataPathPartNext)
    //             pDataPathPartNext = nullptr;
    //         else
    //             pDataPathPartNext++;
    //         // Check if the path part is an array index
    //         int bracketPos = pathPart.indexOf('[');
    //         if (bracketPos >= 0)
    //         {
    //             // Get array index
    //             arrayIdx = pathPart.substring(bracketPos + 1).toInt();
    //             pathPart = pathPart.substring(0, bracketPos);
    //             LOG_I("test____getString", "pathPart %s arrayIdx %d", pathPart.c_str(), arrayIdx);
    //         }
    //         else
    //         {
    //             arrayIdx = -1;
    //             LOG_I("test____getString", "pathPart %s", pathPart.c_str());
    //         }
    //     }

    //     // If we're at the end of the path then return the value
    //     if (pathPart.isEmpty())
    //     {
    //         return getCurrentElement(pJsonDocPos, arrayIdx);
    //     }

    //     // Walk through JSON doc
    //     if (*pJsonDocPos == '{')
    //     {
            

    //         // Skip to end of object
    //         int numBraces = 1;
    //         while (numBraces > 0)
    //         {
    //             pJsonDocPos++;
    //             if (*pJsonDocPos == '{')
    //                 numBraces++;
    //             else if (*pJsonDocPos == '}')
    //                 numBraces--;
    //         }
    //     }
    //     else if (*pJsonDocPos == '[')
    //     {
    //         // Skip to end of array
    //         int numBrackets = 1;
    //         while (numBrackets > 0)
    //         {
    //             pJsonDocPos++;
    //             if (*pJsonDocPos == '[')
    //                 numBrackets++;
    //             else if (*pJsonDocPos == ']')
    //                 numBrackets--;
    //         }
    //     }
    //     else if (*pJsonDocPos == '"')
    //     {
    //         const char* pElemStart = pJsonDocPos;
    //         // Skip to end of string
    //         pJsonDocPos++;
    //         while (*pJsonDocPos && (*pJsonDocPos != '"'))
    //             pJsonDocPos++;

    //         // Check if this is the key we are looking for
    //         if (pathPart.length() > 0)
    //         {
    //             // Check if this is the key we are looking for
    //             if (strncmp(pJsonDocPos, pathPart.c_str(), pathPart.length()) == 0)
    //             {
    //                 // Check if this is the end of the key
    //                 if ((pJsonDocPos[pathPart.length()] == ':') || (pJsonDocPos[pathPart.length()] == ','))
    //                 {
    //                     // Skip to end of key
    //                     pJsonDocPos += pathPart.length();
    //                     // Skip to start of value
    //                     while (*pJsonDocPos && (*pJsonDocPos != ':'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == ':')
    //                         pJsonDocPos += 1;
    //                     // Skip to start of value
    //                     while (*pJsonDocPos && (*pJsonDocPos != '"'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == '"')
    //                         pJsonDocPos += 1;
    //                     // Get value
    //                     pElemStart = pJsonDocPos;
    //                     while (*pJsonDocPos && (*pJsonDocPos != '"'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == '"')
    //                     {
    //                         String val = String(pElemStart, pJsonDocPos - pElemStart);
    //                         return val;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    //     else if (*pJsonDocPos == ',')
    //     {
    //         // Skip to next element
    //         pJsonDocPos++;
    //     }
    //     else if (*pJsonDocPos == ':')
    //     {
    //         // Skip to next element
    //         pJsonDocPos++;
    //     }
    //     else if (*pJsonDocPos == '}')
    //     {
    //         // Skip to next element
    //         pJsonDocPos++;
    //     }
    //     else if (*pJsonDocPos == ']')
    //     {
    //         // Skip to next element
    //         pJsonDocPos++;
    //     }
    //     else
    //     {
    //         // Check if this is the key we are looking for
    //         if (pathPart.length() > 0)
    //         {
    //             // Check if this is the key we are looking for
    //             if (strncmp(pJsonDocPos, pathPart.c_str(), pathPart.length()) == 0)
    //             {
    //                 // Check if this is the end of the key
    //                 if ((pJsonDocPos[pathPart.length()] == ':') || (pJsonDocPos[pathPart.length()] == ','))
    //                 {
    //                     // Skip to end of key
    //                     pJsonDocPos += pathPart.length();
    //                     // Skip to start of value
    //                     while (*pJsonDocPos && (*pJsonDocPos != ':'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == ':')
    //                         pJsonDocPos += 1;
    //                     // Skip to start of value
    //                     while (*pJsonDocPos && (*pJsonDocPos != '"'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == '"')
    //                         pJsonDocPos += 1;
    //                     // Get value
    //                     pElemStart = pJsonDocPos;
    //                     while (*pJsonDocPos && (*pJsonDocPos != '"'))
    //                         pJsonDocPos++;
    //                     if (*pJsonDocPos == '"')
    //                     {
    //                         String val = String(pElemStart, pJsonDocPos - pElemStart);
    //                         return val;
    //                     }
    //                 }
    //             }
    //         }
    //         // Skip to next element
    //         pJsonDocPos++;
    //     }
    // }
    // return "UNKNOWN";
