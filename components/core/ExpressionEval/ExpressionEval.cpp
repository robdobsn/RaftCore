/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ExpressionEval
// Handles mathematical expressions with variables
//
// Rob Dobson 2017-2020
// Originally from RBotFirmware
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <algorithm>
#include "Logger.h"
#include "ExpressionEval.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include "RaftJson.h"

// #define DEBUG_EXPRESSION_EVAL 1
// #define DEBUG_EXPRESSION_EVAL_VAR_DETAIL 1

#ifndef NAN
#define NAN (0.0/0.0)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ExpressionEval::ExpressionEval()
{
}

ExpressionEval::~ExpressionEval()
{
    clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set values
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::addVariables(const char* pValsJsonStr, bool append)
{
    // Clear values if required
    if (!append)
        _exprContext.clear();

    // Set the constants into the evaluator
    std::vector<String> initValNames;
    RaftJson valsJson(pValsJsonStr, false);
    valsJson.getKeys("", initValNames);
    for (String& valToAdd : initValNames)
    {
        // Get value
        double val = valsJson.getDouble(valToAdd.c_str(), 0);
#ifdef DEBUG_EXPRESSION_EVAL_VAR_DETAIL
        LOG_I(MODULE_PREFIX, "addVariables var %s val %f", valToAdd.c_str(), val);
#endif
        _exprContext.addVariable(valToAdd.c_str(), val);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add variables
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::addVariables(std::vector<NameValuePairDouble>& nameValuePairs, bool append)
{
    // Clear values if required
    if (!append)
        _exprContext.clear();

    // Set the constants into the evaluator
    for (NameValuePairDouble& nameValPair : nameValuePairs)
    {
#ifdef DEBUG_EXPRESSION_EVAL
        LOG_I(MODULE_PREFIX, "addVariables var %s val %f", nameValPair.name.c_str(), nameValPair.value);
#endif
        _exprContext.addVariable(nameValPair.name.c_str(), nameValPair.value);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::addFunction(const char* fnName, ExprCustomFn0DRD pFn)
{
    _exprContext.addFunction(fnName, (const void*)pFn, 0);
}

void ExpressionEval::addFunction(const char* fnName, ExprCustomFn1DRD pFn)
{
    _exprContext.addFunction(fnName, (const void*)pFn, 1);
}

void ExpressionEval::addFunction(const char* fnName, ExprCustomFn2DRD pFn)
{
    _exprContext.addFunction(fnName, (const void*)pFn, 2);
}

void ExpressionEval::addFunction(const char* fnName, ExprCustomFn3DRD pFn)
{
    _exprContext.addFunction(fnName, (const void*)pFn,3 );
}

void ExpressionEval::addFunction(const char* fnName, ExprCustomFn4DRD pFn)
{
    _exprContext.addFunction(fnName, (const void*)pFn, 4);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add expressions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ExpressionEval::addExpressions(const char* pExpr, uint32_t& errorLine)
{
    // Clear string variables
    _stringConsts.clear();

    // Iterate through the expressions (split by \r or \n)
    String varName;
    String curToken;
    bool compileOk = true;
    uint32_t lineNum = 1;
    errorLine = 0;
    StatementFlowType flowType = FLOW_TYPE_NONE;
    bool assignmentFound = false;
    bool expressionStarted = false;
    bool flowControlFound = false;
    bool ignoreRestOfLine = false;
    for (const char* pCh = pExpr; *pCh; pCh++)
    {
        // Handle EOL
        if ((*pCh == '\r') || (*pCh == '\n'))
        {
            if (!compileAndStore(curToken, varName, flowType, lineNum))
            {
                errorLine = lineNum;
                compileOk = false;
                break;
            }
            ignoreRestOfLine = false;
            curToken = "";
            varName = "";
            flowType = FLOW_TYPE_NONE;
            assignmentFound = false;
            expressionStarted = false;
            flowControlFound = false;
            if (*pCh == '\n')
                lineNum++;
            continue;
        }
        // Discard whitespace, etc
        if ((*pCh <= 32) || (*pCh >= 127))
            continue;

        // Ignore rest of line after a comment
        if (ignoreRestOfLine || (*pCh == '#'))
        {
            ignoreRestOfLine = true;
            continue;
        }

        // Check for chars which indicate part of expression
        if ((*pCh == '<') || (*pCh == '>') || (*pCh == '!'))
            expressionStarted = true;
        
        // Check for assignment
        if (!assignmentFound && !expressionStarted && ((*pCh == '=') && (*(pCh+1) != '=')))
        {
            // Add assigned var
            varName = curToken;
            curToken = "";
            assignmentFound = true;
            _exprContext.addVariable(varName.c_str(), 0, false);
        }

        // Check for flow control
        else if (!flowControlFound && (*pCh == ':'))
        {
            // Check flow control type
            if (curToken.equalsIgnoreCase("if")) {
                flowType = FLOW_TYPE_IF;
            } else if (curToken.equalsIgnoreCase("else")) {
                flowType = FLOW_TYPE_ELSE;
            } else if (curToken.equalsIgnoreCase("end")) {
                flowType = FLOW_TYPE_END;
            } else if (curToken.equalsIgnoreCase("while")) {
                flowType = FLOW_TYPE_WHILE;
            }
            curToken = "";
            flowControlFound = true;
        }

        // < max expression length
        else if (curToken.length() < MAX_EXPR_LEN)
        {
            // Add to token
            curToken += *pCh;
        }
    }

    // Handle last expression (if there is one)
    if (compileOk)
    {
        compileOk = compileAndStore(curToken, varName, flowType, lineNum);
        if (!compileOk)
            errorLine = lineNum;
    }
    return compileOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Evaluate statements
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::evalStatements(const char* pImmutableVarsJsonStr)
{
    // Get the names of immutable variables 
    std::vector<String> immutableVarNames;
    if (pImmutableVarsJsonStr)
    {
        RaftJson immutableVars(pImmutableVarsJsonStr, false);
        immutableVars.getKeys("", immutableVarNames);
    }

#ifdef DEBUG_EXPRESSION_EVAL
    // Debug
    LOG_I(MODULE_PREFIX, "evalStatements numStatements %d", _compiledStatements.size());
#endif

    // Go through all the statements and evaluate
    uint32_t programCounter = 0;
    for (unsigned int evalCount = 0; evalCount < MAX_EXPRESSION_EVAL_PROC_LINES; evalCount++)
    {
        // Check programCounter valid
        if (programCounter >= _compiledStatements.size())
            break;

        // Check if there is an expression
        double val = 0;
        if (_compiledStatements[programCounter]._pCompExpr)
        {
            // Compute value of expression
            val = te_eval(_compiledStatements[programCounter]._pCompExpr);
        }

#ifdef DEBUG_EXPRESSION_EVAL
        // Debug
        LOG_I(MODULE_PREFIX, "evalStatements pc %d eval %.3f assignVar %s flowType %s", 
                    programCounter, val, 
                    _compiledStatements[programCounter]._assignedVarName.length() > 0 ? 
                                _compiledStatements[programCounter]._assignedVarName.c_str() : "NONE",
                    getFlowTypeStr(_compiledStatements[programCounter]._flowType));
#endif

        // Check for assignment
        if (_compiledStatements[programCounter]._assignedVarName.length() != 0)
        {
            // Check if assigned variable name is in the list of immutable variables
            if(std::find(immutableVarNames.begin(), immutableVarNames.end(), 
                                _compiledStatements[programCounter]._assignedVarName) == immutableVarNames.end()) 
            {
                // If not set the value of the variable
                _exprContext.addVariable(_compiledStatements[programCounter]._assignedVarName.c_str(), val, true);

#ifdef DEBUG_EXPRESSION_EVAL
                LOG_I(MODULE_PREFIX, "expr %d: var %s = exprRslt %f", 
                                programCounter, _compiledStatements[programCounter]._assignedVarName.c_str(), val);
#endif
            }
            else
            {
#ifdef DEBUG_EXPRESSION_EVAL
                LOG_I(MODULE_PREFIX, "expr %d: var %s NOT ASSIGNED AS IMMUTABLE exprRslt %f", 
                                programCounter, _compiledStatements[programCounter]._assignedVarName.c_str(), val);
#endif

            }
        }

        // Check for flow control
        switch (_compiledStatements[programCounter]._flowType)
        {
            case FLOW_TYPE_NONE:
            default:
            {
                programCounter++;
                break;
            }
            case FLOW_TYPE_IF:
            {
                // Check if we should branch
                if (val == 0)
                    programCounter = findMatchingFlowUnit(programCounter);
                else
                    programCounter++;
                break;
            }
            case FLOW_TYPE_ELSE:
            {
                // Always go to statement beyond matching end
                programCounter = findMatchingFlowUnit(programCounter);
                break;
            }
            case FLOW_TYPE_END:
            {
                // Handle match
                programCounter = findMatchingFlowUnit(programCounter);
                break;
            }
            case FLOW_TYPE_WHILE:
            {
                // Check if we're done
                if (val == 0)
                    programCounter = findMatchingFlowUnit(programCounter);
                else
                    programCounter++;
                break;
            }
        }

        // Check programCounter valid
        if (programCounter >= _compiledStatements.size())
            break;
    }

#ifdef DEBUG_EXPRESSION_EVAL
    if (programCounter >= _compiledStatements.size()) {
        LOG_I(MODULE_PREFIX, "evalStatements execution finished (pc at end)"); 
    } else {
        LOG_I(MODULE_PREFIX, "evalStatements execution terminated (too many steps)"); 
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find matching flow unit
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t ExpressionEval::findMatchingFlowUnit(uint32_t pc)
{
    // Check programCounter valid
    if (pc >= _compiledStatements.size())
        return _compiledStatements.size();

    // Current flow type & initial pc
    StatementFlowType startFlowType = _compiledStatements[pc]._flowType;
    uint32_t initialPC = pc;

    // Iterate in appropriate direction
    if (startFlowType == FLOW_TYPE_END)
    {
        // Iterate backwards
        uint32_t nestLevel = 0;
        while(pc >= 1)
        {
            pc--;
            StatementFlowType ft = _compiledStatements[pc]._flowType;
            if (ft == FLOW_TYPE_END)
            {
                // Handle nested IF/WHILE statements
                nestLevel++;
            }
            else if (nestLevel > 0)
            {
                // If we're in a nested level then ignore all but other flow start statements
                if ((ft == FLOW_TYPE_IF) || (ft == FLOW_TYPE_WHILE))
                    nestLevel--;
            }
            else if (ft == FLOW_TYPE_IF)
            {
                // If we find the matching statement was if then just continue from where we were
                return initialPC + 1;
            }
            else if (ft == FLOW_TYPE_WHILE)
            {
                // If matching statement was while then re-evaluate the expression
                return pc;
            }
        }
    }
    else
    {
        // Iterate forwards
        uint32_t nestLevel = 0;
        while(pc < _compiledStatements.size())
        {
            pc++;
            StatementFlowType ft = _compiledStatements[pc]._flowType;

            // LOG_I(MODULE_PREFIX, "findMatchingFlowUnit pc %d type %s", pc, getFlowTypeStr(ft));

            if ((ft == FLOW_TYPE_IF) || (ft == FLOW_TYPE_WHILE))
            {
                nestLevel++;
            }
            else if (nestLevel > 0)
            {
                // If we're in a nested level then ignore all but ends
                if (ft == FLOW_TYPE_END)
                    nestLevel--;
            }
            else if (ft == FLOW_TYPE_ELSE)
            {
                // Must be in an if whose condition is false so continue from next line
                return pc + 1;
            }
            else if (ft == FLOW_TYPE_END)
            {
                // Found the matching end
                return pc + 1;
            }
        }
    }

    // LOG_I(MODULE_PREFIX, "findMatchingFlowUnit failed returning %d", _compiledStatements.size());

    // It's a bug if we get here
    return _compiledStatements.size();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add to statements
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ExpressionEval::compileAndStore(String& expr, const String& varName, 
            StatementFlowType flowType, uint32_t lineNum)
{
    // Check we haven't reached the limit of statements
    if (_compiledStatements.size() >= MAX_EXPRESSIONS)
        return false;

    // Find and replace string variables in the expressions
    findAndReplaceStringConsts(expr);

    // Add any undefined global variables
    addAnyUndefinedGlobalVars(expr);

    // Get context (vars and functions)
    std::vector<te_variable> varsContext;
    _exprContext.getTEVars(varsContext);

    // Debug TEVars
#ifdef DEBUG_EXPRESSION_EVAL
    LOG_I(MODULE_PREFIX, "compileAndStore line %d varName %s expr %s flowType %s numTEVars %d", lineNum,
                    varName.c_str(), expr.length() > 0 ? expr.c_str() : "EMPTY", getFlowTypeStr(flowType), varsContext.size());
#ifdef DEBUG_EXPRESSION_EVAL_VAR_DETAIL
    for (int i = 0; i < varsContext.size(); i++)
    {
        LOG_I(MODULE_PREFIX, "compileAndStore line %d var %d name %s type %s val %f", lineNum,
                i, varsContext[i].name, ExpressionContext::getVarTypeStr(varsContext[i].type),
                varsContext[i].type == TE_VARIABLE ? *(double*)varsContext[i].address : 0);
    }
#endif
#endif

    // Compile the expression if there is one
    int err = 0;
    te_expr* pCompiledExpr = NULL;
    if (expr.length() > 0)
        pCompiledExpr = te_compile(expr.c_str(), varsContext.data(), varsContext.size(), &err);

    // Store the statement if there is anything valid
    if ((pCompiledExpr != NULL) || (varName.length() > 0) || (flowType != FLOW_TYPE_NONE))
    {
        CompiledStatement compiledStatement;
        compiledStatement._pCompExpr = pCompiledExpr;
        compiledStatement._assignedVarName = varName;
        compiledStatement._flowType = flowType;
        _compiledStatements.push_back(compiledStatement);
#ifdef DEBUG_EXPRESSION_EVAL
        LOG_I(MODULE_PREFIX, "compileAndStore line %d OK %s numVars %d compiledExprs %d", lineNum,
                expr.c_str(), varsContext.size(), _compiledStatements.size());
#endif
    }
    else
    {
#ifdef DEBUG_EXPRESSION_EVAL
        LOG_I(MODULE_PREFIX, "compileAndStore ignored line %d exprLen %d compiledExpr %s", 
                    lineNum, expr.length(), pCompiledExpr != NULL ? "OK" : "NULL");
#endif
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clean up
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::clear()
{
    _exprContext.clear();
    for (unsigned int i = 0; i < _compiledStatements.size(); i++)
        te_free(_compiledStatements[i]._pCompExpr);
    _compiledStatements.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find and replace string consts
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::findAndReplaceStringConsts(String& exprStr)
{
    // Check if there are string variables
    bool continueSearch = true;
    while(continueSearch)
    {
        continueSearch = false;
        int strStartPos = -1;
        for (int i = 0; i < exprStr.length(); i++)
        {
            if (exprStr.charAt(i) == '"')
            {
                if (strStartPos >= 0)
                {
                    // Found a string const - get the name
                    String strConst = exprStr.substring(strStartPos+1, i);
                    // Replace the entire string (including inverted commas with a new variable
                    exprStr.replace("\"" + strConst + "\"", String(_stringConsts.size()));
                    // Add the const string to the vector
                    _stringConsts.push_back(strConst);
                    // Search again for others
                    continueSearch = true;
                    break;
                }
                else
                {
                    strStartPos = i;
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add any undefined global variables
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::addAnyUndefinedGlobalVars(String& exprStr)
{
    // Check if there are any undefined global variables
    bool inVar = false;
    String varName;
    for (int i = 0; i < exprStr.length(); i++)
    {
        if (exprStr.charAt(i) == '$')
        {
            inVar = true;
            varName += exprStr.charAt(i);
        } 
        else if (inVar)
        {
            if (isalnum(exprStr.charAt(i)) || (exprStr.charAt(i) == '_'))
            {
                varName += exprStr.charAt(i);
            }
            else
            {
                // Found the end of the variable name
                inVar = false;
                // Add the variable if it doesn't exist
                _exprContext.addVariable(varName.c_str(), NAN, false);
                varName = "";
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debugging function for expressions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ExpressionEval::debugGetExprInfo(const te_expr *n, int depth, bool logExpr, uint32_t& curCompiledSize) 
{
    // Size of data
    curCompiledSize += sizeof(te_expr);

    // Indent appropriately
    String logIndent;
    char logTmp[100];
    snprintf(logTmp, sizeof(logTmp), "%*s", depth, "");
    logIndent = logTmp;

    #define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)
    #define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
    enum { TE_CONSTANT = 1 };

    // Handle node type
	switch (TYPE_MASK(n->type)) 
    {
        case TE_CONSTANT:
            if (logExpr)
            {
                LOG_I(MODULE_PREFIX, "%s%f", logIndent.c_str(), n->value);
            }
            break;
        case TE_VARIABLE: 
            if (logExpr)
            {
                LOG_I(MODULE_PREFIX, "%sbound %p", logIndent.c_str(), n->bound);
            }
            break;
        case TE_FUNCTION0: 
        case TE_FUNCTION1: 
        case TE_FUNCTION2: 
        case TE_FUNCTION3:
        case TE_FUNCTION4: 
        case TE_FUNCTION5: 
        case TE_FUNCTION6: 
        case TE_FUNCTION7:
        case TE_CLOSURE0: 
        case TE_CLOSURE1: 
        case TE_CLOSURE2: 
        case TE_CLOSURE3:
        case TE_CLOSURE4: 
        case TE_CLOSURE5: 
        case TE_CLOSURE6: 
        case TE_CLOSURE7:
        {
            int arity = ARITY(n->type);
            snprintf(logTmp, sizeof(logTmp), "f%d", arity);
            logIndent += logTmp;
            for (int i = 0; i < arity; i++) 
            {
                snprintf(logTmp, sizeof(logTmp), " %p", n->parameters[i]);
                logIndent += logTmp;
            }
            if (logExpr)
            {
                LOG_I(MODULE_PREFIX, "%s", logIndent.c_str());
            }
            for (int i = 0; i < arity; i++) 
            {
                debugGetExprInfo((const te_expr *)(n->parameters[i]), depth + 1, logExpr, curCompiledSize);
            }
            break;
        }
	}
}