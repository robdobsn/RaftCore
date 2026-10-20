/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeCodeInterpreter.cpp
//
// See DeviceTypeCodeInterpreter.h
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeCodeInterpreter.h"

#ifdef RAFT_RUNTIME_DECODE_CODE

#include <cerrno>
#include <climits>
#include <ctype.h>
#include <new>
#include <stdlib.h>
#include <string.h>

using NumType = DeviceTypeCodeProgram::NumType;
using VT = DeviceTypeCodeProgram::VT;
using Val = DeviceTypeCodeProgram::Val;
using Node = DeviceTypeCodeProgram::Node;

namespace
{
    // Operations
    enum Op : uint8_t
    {
        // Expressions
        OP_LIT, OP_VAR, OP_BUF, OP_OUT, OP_INIT, OP_CAST, OP_NEG, OP_POS, OP_NOT, OP_BNOT,
        OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_SHL, OP_SHR, OP_AND, OP_OR, OP_XOR,
        OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE, OP_LAND, OP_LOR,
        OP_ASSIGN, OP_PREINC, OP_PREDEC, OP_POSTINC, OP_POSTDEC, OP_ARRAY_WRITE,
        // Statements
        OP_BLOCK, OP_DECL, OP_EXPR, OP_IF, OP_WHILE, OP_RETURN, OP_NEXT, OP_EMPTY,
    };

    const uint32_t MAX_NESTING = 32;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Types and values
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////

    VT promotedType(NumType st)
    {
        switch (st)
        {
            case NumType::Uint32: return VT::U32;
            case NumType::Int64: return VT::I64;
            case NumType::Uint64: return VT::U64;
            case NumType::Float: return VT::F32;
            case NumType::Double: return VT::F64;
            default: return VT::I32;
        }
    }

    bool isFloatVT(VT t)
    {
        return (t == VT::F32) || (t == VT::F64);
    }

    // The usual arithmetic conversions
    VT commonType(VT a, VT b)
    {
        if ((a == VT::F64) || (b == VT::F64))
            return VT::F64;
        if ((a == VT::F32) || (b == VT::F32))
            return VT::F32;
        if (a == b)
            return a;
        auto rank = [](VT t) { return ((t == VT::I64) || (t == VT::U64)) ? 2 : 1; };
        auto isSigned = [](VT t) { return (t == VT::I32) || (t == VT::I64); };
        if (isSigned(a) == isSigned(b))
            return rank(a) >= rank(b) ? a : b;
        VT u = isSigned(a) ? b : a;
        VT s = isSigned(a) ? a : b;
        if (rank(u) >= rank(s))
            return u;
        if (rank(s) > rank(u))
            return s;
        return (s == VT::I64) ? VT::U64 : VT::U32;
    }

    // A value as C++ type T (the C++ conversion from its type)
    template <typename T>
    T as(const Val& v)
    {
        switch (v.t)
        {
            case VT::I32: return (T)(int32_t)v.i;
            case VT::U32: return (T)(uint32_t)v.i;
            case VT::I64: return (T)v.i;
            case VT::U64: return (T)v.u;
            case VT::F32: return (T)v.f;
            default: return (T)v.d;
        }
    }

    Val makeI32(int32_t x) { Val v; v.t = VT::I32; v.i = x; return v; }
    Val makeU32(uint32_t x) { Val v; v.t = VT::U32; v.i = x; return v; }
    Val makeI64(int64_t x) { Val v; v.t = VT::I64; v.i = x; return v; }
    Val makeU64(uint64_t x) { Val v; v.t = VT::U64; v.u = x; return v; }
    Val makeF32(float x) { Val v; v.t = VT::F32; v.f = x; return v; }
    Val makeF64(double x) { Val v; v.t = VT::F64; v.d = x; return v; }

    // Convert to a storage type (a variable, field or cast), then promote
    Val narrowTo(const Val& v, NumType st)
    {
        switch (st)
        {
            case NumType::Int8: return makeI32(as<int8_t>(v));
            case NumType::Uint8: return makeI32(as<uint8_t>(v));
            case NumType::Int16: return makeI32(as<int16_t>(v));
            case NumType::Uint16: return makeI32(as<uint16_t>(v));
            case NumType::Int32: return makeI32(as<int32_t>(v));
            case NumType::Uint32: return makeU32(as<uint32_t>(v));
            case NumType::Int64: return makeI64(as<int64_t>(v));
            case NumType::Uint64: return makeU64(as<uint64_t>(v));
            case NumType::Float: return makeF32(as<float>(v));
            case NumType::Double: return makeF64(as<double>(v));
            case NumType::Bool: return makeI32(as<bool>(v) ? 1 : 0);
            default: return makeI32(0);
        }
    }

    bool truthy(const Val& v)
    {
        switch (v.t)
        {
            case VT::I32: return (int32_t)v.i != 0;
            case VT::U32: return (uint32_t)v.i != 0;
            case VT::I64: return v.i != 0;
            case VT::U64: return v.u != 0;
            case VT::F32: return v.f != 0;
            default: return v.d != 0;
        }
    }

    // Read and write a struct field of storage type st
    Val loadField(const uint8_t* p, NumType st)
    {
        switch (st)
        {
            case NumType::Int8: { int8_t x; memcpy(&x, p, 1); return makeI32(x); }
            case NumType::Uint8: { uint8_t x; memcpy(&x, p, 1); return makeI32(x); }
            case NumType::Int16: { int16_t x; memcpy(&x, p, 2); return makeI32(x); }
            case NumType::Uint16: { uint16_t x; memcpy(&x, p, 2); return makeI32(x); }
            case NumType::Int32: { int32_t x; memcpy(&x, p, 4); return makeI32(x); }
            case NumType::Uint32: { uint32_t x; memcpy(&x, p, 4); return makeU32(x); }
            case NumType::Int64: { int64_t x; memcpy(&x, p, 8); return makeI64(x); }
            case NumType::Uint64: { uint64_t x; memcpy(&x, p, 8); return makeU64(x); }
            case NumType::Float: { float x; memcpy(&x, p, 4); return makeF32(x); }
            case NumType::Double: { double x; memcpy(&x, p, 8); return makeF64(x); }
            case NumType::Bool: { bool x; memcpy(&x, p, sizeof(bool)); return makeI32(x ? 1 : 0); }
            default: return makeI32(0);
        }
    }

    void storeField(uint8_t* p, NumType st, const Val& v)
    {
        switch (st)
        {
            case NumType::Int8: { int8_t x = as<int8_t>(v); memcpy(p, &x, 1); break; }
            case NumType::Uint8: { uint8_t x = as<uint8_t>(v); memcpy(p, &x, 1); break; }
            case NumType::Int16: { int16_t x = as<int16_t>(v); memcpy(p, &x, 2); break; }
            case NumType::Uint16: { uint16_t x = as<uint16_t>(v); memcpy(p, &x, 2); break; }
            case NumType::Int32: { int32_t x = as<int32_t>(v); memcpy(p, &x, 4); break; }
            case NumType::Uint32: { uint32_t x = as<uint32_t>(v); memcpy(p, &x, 4); break; }
            case NumType::Int64: { int64_t x = as<int64_t>(v); memcpy(p, &x, 8); break; }
            case NumType::Uint64: { uint64_t x = as<uint64_t>(v); memcpy(p, &x, 8); break; }
            case NumType::Float: { float x = as<float>(v); memcpy(p, &x, 4); break; }
            case NumType::Double: { double x = as<double>(v); memcpy(p, &x, 8); break; }
            case NumType::Bool: { bool x = as<bool>(v); memcpy(p, &x, sizeof(bool)); break; }
            default: break;
        }
    }

    uint32_t storeSize(NumType st)
    {
        switch (st)
        {
            case NumType::Int8: case NumType::Uint8: return 1;
            case NumType::Int16: case NumType::Uint16: return 2;
            case NumType::Int32: case NumType::Uint32: case NumType::Float: return 4;
            case NumType::Int64: case NumType::Uint64: case NumType::Double: return 8;
            case NumType::Bool: return sizeof(bool);
            default: return 0;
        }
    }

    // A binary operation on two values of the same (common) type, or a shift (the left operand's type)
    template <typename T, typename U>
    T wrapAdd(T a, T b) { return (T)((U)a + (U)b); }
    template <typename T, typename U>
    T wrapSub(T a, T b) { return (T)((U)a - (U)b); }
    template <typename T, typename U>
    T wrapMul(T a, T b) { return (T)((U)a * (U)b); }

    template <typename T, typename U>
    Val intBinary(uint8_t op, T a, T b, Val (*make)(T))
    {
        switch (op)
        {
            case OP_ADD: return make(wrapAdd<T, U>(a, b));
            case OP_SUB: return make(wrapSub<T, U>(a, b));
            case OP_MUL: return make(wrapMul<T, U>(a, b));
            case OP_DIV:
                if (b == 0)
                    return make(0);
                if (((T)-1 < 0) && (b == (T)-1))
                    return make(wrapSub<T, U>(0, a));
                return make(a / b);
            case OP_MOD:
                if ((b == 0) || (((T)-1 < 0) && (b == (T)-1)))
                    return make(0);
                return make(a % b);
            case OP_AND: return make(a & b);
            case OP_OR: return make(a | b);
            case OP_XOR: return make(a ^ b);
            case OP_LT: return makeI32(a < b);
            case OP_LE: return makeI32(a <= b);
            case OP_GT: return makeI32(a > b);
            case OP_GE: return makeI32(a >= b);
            case OP_EQ: return makeI32(a == b);
            case OP_NE: return makeI32(a != b);
            default: return make(0);
        }
    }

    template <typename T>
    Val floatBinary(uint8_t op, T a, T b, Val (*make)(T))
    {
        switch (op)
        {
            case OP_ADD: return make(a + b);
            case OP_SUB: return make(a - b);
            case OP_MUL: return make(a * b);
            case OP_DIV: return make(a / b);
            case OP_LT: return makeI32(a < b);
            case OP_LE: return makeI32(a <= b);
            case OP_GT: return makeI32(a > b);
            case OP_GE: return makeI32(a >= b);
            case OP_EQ: return makeI32(a == b);
            case OP_NE: return makeI32(a != b);
            default: return make(0);
        }
    }

    Val binary(uint8_t op, VT type, const Val& lhs, const Val& rhs)
    {
        if ((op == OP_SHL) || (op == OP_SHR))
        {
            int64_t count = as<int64_t>(rhs);
            switch (type)
            {
                case VT::I32:
                {
                    int32_t a = as<int32_t>(lhs);
                    uint32_t n = (uint32_t)count & 31;
                    return makeI32((op == OP_SHL) ? (int32_t)((uint32_t)a << n) : (a >> n));
                }
                case VT::U32:
                {
                    uint32_t a = as<uint32_t>(lhs);
                    uint32_t n = (uint32_t)count & 31;
                    return makeU32((op == OP_SHL) ? (a << n) : (a >> n));
                }
                case VT::I64:
                {
                    int64_t a = as<int64_t>(lhs);
                    uint32_t n = (uint32_t)count & 63;
                    return makeI64((op == OP_SHL) ? (int64_t)((uint64_t)a << n) : (a >> n));
                }
                default:
                {
                    uint64_t a = as<uint64_t>(lhs);
                    uint32_t n = (uint32_t)count & 63;
                    return makeU64((op == OP_SHL) ? (a << n) : (a >> n));
                }
            }
        }
        switch (type)
        {
            case VT::I32: return intBinary<int32_t, uint32_t>(op, as<int32_t>(lhs), as<int32_t>(rhs), makeI32);
            case VT::U32: return intBinary<uint32_t, uint32_t>(op, as<uint32_t>(lhs), as<uint32_t>(rhs), makeU32);
            case VT::I64: return intBinary<int64_t, uint64_t>(op, as<int64_t>(lhs), as<int64_t>(rhs), makeI64);
            case VT::U64: return intBinary<uint64_t, uint64_t>(op, as<uint64_t>(lhs), as<uint64_t>(rhs), makeU64);
            case VT::F32: return floatBinary<float>(op, as<float>(lhs), as<float>(rhs), makeF32);
            default: return floatBinary<double>(op, as<double>(lhs), as<double>(rhs), makeF64);
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Lexer (the tokens of PseudocodeHandler.lexer; an identifier may contain dots, e.g. out.x)
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////

    enum class Tok : uint8_t
    {
        End, Id, KwInt, KwFloat, KwReturn, KwWhile, KwNext, NumInt, NumHex, NumFloat, Punct, Bad,
    };

    struct Token
    {
        Tok kind = Tok::End;
        String text;
        uint32_t pos = 0;
    };

    bool lex(const char* pCode, std::vector<Token>& tokens, String& problem)
    {
        static const char* PUNCT[] = {
            "<<=", ">>=", "&&", "||", "<<", ">>", "<=", ">=", "==", "!=", "+=", "-=", "*=", "/=", "%=", "&=", "|=",
            "^=", "++", "--", "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=", ";", ",", "(", ")",
            "{", "}", "[", "]",
        };
        const char* p = pCode ? pCode : "";
        const char* pStart = p;
        while (*p)
        {
            if ((*p == ' ') || (*p == '\t') || (*p == '\n') || (*p == '\r'))
            {
                p++;
                continue;
            }
            Token tok;
            tok.pos = (uint32_t)(p - pStart);
            if (isalpha((unsigned char)*p) || (*p == '_'))
            {
                const char* q = p;
                while (isalnum((unsigned char)*q) || (*q == '_') ||
                            ((*q == '.') && (isalpha((unsigned char)q[1]) || (q[1] == '_'))))
                    q++;
                tok.text = String(p).substring(0, q - p);
                if (tok.text == "int")
                    tok.kind = Tok::KwInt;
                else if (tok.text == "float")
                    tok.kind = Tok::KwFloat;
                else if (tok.text == "return")
                    tok.kind = Tok::KwReturn;
                else if (tok.text == "while")
                    tok.kind = Tok::KwWhile;
                else if (tok.text == "next")
                    tok.kind = Tok::KwNext;
                else
                    tok.kind = Tok::Id;
                p = q;
            }
            else if (isdigit((unsigned char)*p))
            {
                const char* q = p;
                if ((p[0] == '0') && (p[1] == 'x') && isxdigit((unsigned char)p[2]))
                {
                    q = p + 2;
                    while (isxdigit((unsigned char)*q))
                        q++;
                    tok.kind = Tok::NumHex;
                }
                else
                {
                    while (isdigit((unsigned char)*q))
                        q++;
                    tok.kind = Tok::NumInt;
                    if (*q == '.')
                    {
                        q++;
                        while (isdigit((unsigned char)*q))
                            q++;
                        tok.kind = Tok::NumFloat;
                    }
                }
                tok.text = String(p).substring(0, q - p);
                p = q;
                if (isalnum((unsigned char)*p) || (*p == '_') || (*p == '.'))
                {
                    problem = "invalid number at " + String(tok.pos);
                    return false;
                }
            }
            else
            {
                bool found = false;
                for (const char* pPunct : PUNCT)
                {
                    size_t len = strlen(pPunct);
                    if (strncmp(p, pPunct, len) == 0)
                    {
                        tok.kind = Tok::Punct;
                        tok.text = pPunct;
                        p += len;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    problem = "unexpected character '" + String(*p) + "' at " + String(tok.pos);
                    return false;
                }
            }
            tokens.push_back(tok);
        }
        Token end;
        end.pos = (uint32_t)(p - pStart);
        tokens.push_back(end);
        return true;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compiler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class DeviceTypeCodeCompiler
{
public:
    DeviceTypeCodeCompiler(DeviceTypeCodeProgram& prog, const std::vector<Token>& tokens) : _prog(prog), _toks(tokens)
    {
    }

    bool compileProgram(String& problem)
    {
        _scopes.emplace_back();
        std::vector<int32_t> stmts;
        while (!atEnd())
        {
            int32_t stmt = statement();
            if (stmt < 0)
                break;
            stmts.push_back(stmt);
        }
        if (_failed)
        {
            problem = _problem;
            return false;
        }
        _prog._root = makeBlock(stmts);
        return true;
    }

private:
    DeviceTypeCodeProgram& _prog;
    const std::vector<Token>& _toks;
    uint32_t _idx = 0;
    uint32_t _depth = 0;
    bool _failed = false;
    String _problem;
    struct ScopeEntry
    {
        String name;
        int32_t slot;
    };
    std::vector<std::vector<ScopeEntry>> _scopes;

    // Tokens
    const Token& peek(uint32_t ahead = 0) const
    {
        uint32_t i = _idx + ahead;
        return _toks[i < _toks.size() ? i : _toks.size() - 1];
    }
    bool atEnd() const
    {
        return _failed || (peek().kind == Tok::End);
    }
    bool isPunct(const char* p, uint32_t ahead = 0) const
    {
        return (peek(ahead).kind == Tok::Punct) && (peek(ahead).text == p);
    }
    bool acceptPunct(const char* p)
    {
        if (!isPunct(p))
            return false;
        _idx++;
        return true;
    }
    bool expectPunct(const char* p)
    {
        if (acceptPunct(p))
            return true;
        fail(String("expected '") + p + "'");
        return false;
    }
    int32_t fail(const String& msg)
    {
        if (!_failed)
        {
            _failed = true;
            _problem = msg + " at " + String(peek().pos);
        }
        return -1;
    }

    // Nodes
    int32_t addNode(const Node& node)
    {
        _prog._nodes.push_back(node);
        return (int32_t)_prog._nodes.size() - 1;
    }
    int32_t makeNode(uint8_t op, VT type, int32_t a = -1, int32_t b = -1, int32_t c = -1)
    {
        Node n;
        n.op = op;
        n.type = type;
        n.a = a;
        n.b = b;
        n.c = c;
        return addNode(n);
    }
    int32_t makeBlock(const std::vector<int32_t>& stmts)
    {
        int32_t start = (int32_t)_prog._lists.size();
        _prog._lists.insert(_prog._lists.end(), stmts.begin(), stmts.end());
        return makeNode(OP_BLOCK, VT::I32, start, (int32_t)stmts.size());
    }
    const Node& node(int32_t idx) const
    {
        return _prog._nodes[idx];
    }

    // Type names (declarations and casts)
    bool typeNameAt(uint32_t ahead, NumType& type) const
    {
        const Token& t = peek(ahead);
        if (t.kind == Tok::KwInt) { type = NumType::Int32; return true; }
        if (t.kind == Tok::KwFloat) { type = NumType::Float; return true; }
        if (t.kind != Tok::Id) return false;
        if (t.text == "int8_t") { type = NumType::Int8; return true; }
        if (t.text == "uint8_t") { type = NumType::Uint8; return true; }
        if (t.text == "int16_t") { type = NumType::Int16; return true; }
        if (t.text == "uint16_t") { type = NumType::Uint16; return true; }
        if (t.text == "int32_t") { type = NumType::Int32; return true; }
        if (t.text == "uint32_t") { type = NumType::Uint32; return true; }
        return false;
    }

    // Variables
    int32_t findVar(const String& name) const
    {
        for (int32_t s = (int32_t)_scopes.size() - 1; s >= 0; s--)
            for (const auto& entry : _scopes[s])
                if (entry.name == name)
                    return entry.slot;
        return -1;
    }

    // Statements
    int32_t statement()
    {
        if (++_depth > MAX_NESTING)
            return fail("nested too deeply");
        int32_t result = statementInner();
        _depth--;
        return result;
    }

    int32_t statementInner()
    {
        const Token& t = peek();
        if (acceptPunct("{"))
        {
            _scopes.emplace_back();
            std::vector<int32_t> stmts;
            while (!_failed && !isPunct("}"))
            {
                if (peek().kind == Tok::End)
                    return fail("missing '}'");
                int32_t stmt = statement();
                if (stmt < 0)
                    return -1;
                stmts.push_back(stmt);
            }
            _scopes.pop_back();
            if (!expectPunct("}"))
                return -1;
            return makeBlock(stmts);
        }
        if (acceptPunct(";"))
            return makeNode(OP_EMPTY, VT::I32);
        if ((t.kind == Tok::Id) && (t.text == "if"))
        {
            _idx++;
            if (!expectPunct("("))
                return -1;
            int32_t cond = expression();
            if ((cond < 0) || !expectPunct(")"))
                return -1;
            int32_t thenStmt = statement();
            if (thenStmt < 0)
                return -1;
            int32_t elseStmt = -1;
            if ((peek().kind == Tok::Id) && (peek().text == "else"))
            {
                _idx++;
                elseStmt = statement();
                if (elseStmt < 0)
                    return -1;
            }
            return makeNode(OP_IF, VT::I32, cond, thenStmt, elseStmt);
        }
        if (t.kind == Tok::KwWhile)
        {
            _idx++;
            if (!expectPunct("("))
                return -1;
            int32_t cond = expression();
            if ((cond < 0) || !expectPunct(")"))
                return -1;
            int32_t body = statement();
            if (body < 0)
                return -1;
            return makeNode(OP_WHILE, VT::I32, cond, body);
        }
        if (t.kind == Tok::KwReturn)
        {
            _idx++;
            int32_t value = -1;
            if (!isPunct(";"))
            {
                value = expression();
                if (value < 0)
                    return -1;
            }
            if (!expectPunct(";"))
                return -1;
            return makeNode(OP_RETURN, VT::I32, value);
        }
        if (t.kind == Tok::KwNext)
        {
            if (_prog._kind != DeviceTypeCodeProgram::Kind::Decode)
                return fail("next is only for decode code");
            _idx++;
            if (!expectPunct(";"))
                return -1;
            return makeNode(OP_NEXT, VT::I32);
        }
        NumType declType;
        if (typeNameAt(0, declType) && (peek(1).kind == Tok::Id))
        {
            _idx++;
            std::vector<int32_t> decls;
            while (true)
            {
                if (peek().kind != Tok::Id)
                    return fail("expected a variable name");
                String name = peek().text;
                NumType nameAsType;
                if ((name.indexOf('.') >= 0) || (name == "buf") || (name == "if") || (name == "else") ||
                            typeNameAt(0, nameAsType) || (findVarInScope(name) >= 0))
                    return fail("can't declare " + name);
                _idx++;
                int32_t init = -1;
                if (acceptPunct("="))
                {
                    init = assignment();
                    if (init < 0)
                        return -1;
                }
                int32_t slot = (int32_t)_prog._slotTypes.size();
                _prog._slotTypes.push_back(declType);
                _scopes.back().push_back({name, slot});
                Node n;
                n.op = OP_DECL;
                n.storeType = declType;
                n.a = slot;
                n.b = init;
                decls.push_back(addNode(n));
                if (!acceptPunct(","))
                    break;
            }
            if (!expectPunct(";"))
                return -1;
            return (decls.size() == 1) ? decls[0] : makeBlock(decls);
        }
        int32_t expr = expression();
        if ((expr < 0) || !expectPunct(";"))
            return -1;
        return makeNode(OP_EXPR, VT::I32, expr);
    }

    int32_t findVarInScope(const String& name) const
    {
        for (const auto& entry : _scopes.back())
            if (entry.name == name)
                return entry.slot;
        return -1;
    }

    // Expressions
    int32_t expression()
    {
        return assignment();
    }

    bool isLvalue(int32_t idx) const
    {
        uint8_t op = node(idx).op;
        return (op == OP_VAR) || (op == OP_OUT) || (op == OP_INIT);
    }

    int32_t assignment()
    {
        if (++_depth > MAX_NESTING)
            return fail("nested too deeply");
        int32_t result = assignmentInner();
        _depth--;
        return result;
    }

    int32_t assignmentInner()
    {
        int32_t lhs = logicalOr();
        if (lhs < 0)
            return -1;
        static const struct { const char* punct; int32_t op; } ASSIGN_OPS[] = {
            {"=", -1}, {"+=", OP_ADD}, {"-=", OP_SUB}, {"*=", OP_MUL}, {"/=", OP_DIV}, {"%=", OP_MOD},
            {"&=", OP_AND}, {"|=", OP_OR}, {"^=", OP_XOR}, {"<<=", OP_SHL}, {">>=", OP_SHR},
        };
        for (const auto& assignOp : ASSIGN_OPS)
        {
            if (!isPunct(assignOp.punct))
                continue;
            if (!isLvalue(lhs))
                return fail("can't assign to this");
            _idx++;
            int32_t rhs = assignment();
            if (rhs < 0)
                return -1;
            // A compound assignment's operation must be one C++ allows on these types
            if ((assignOp.op >= 0) && !checkBinaryTypes((uint8_t)assignOp.op, node(lhs).type, node(rhs).type))
                return -1;
            return makeNode(OP_ASSIGN, node(lhs).type, lhs, rhs, assignOp.op);
        }
        return lhs;
    }

    bool checkBinaryTypes(uint8_t op, VT a, VT b)
    {
        bool intOnly = (op == OP_MOD) || (op == OP_SHL) || (op == OP_SHR) || (op == OP_AND) || (op == OP_OR) || (op == OP_XOR);
        if (intOnly && (isFloatVT(a) || isFloatVT(b)))
        {
            fail("integer operation on a float");
            return false;
        }
        return true;
    }

    int32_t binaryNode(uint8_t op, int32_t lhs, int32_t rhs)
    {
        if ((lhs < 0) || (rhs < 0) || !checkBinaryTypes(op, node(lhs).type, node(rhs).type))
            return -1;
        VT type;
        if ((op == OP_SHL) || (op == OP_SHR))
            type = node(lhs).type;
        else if ((op >= OP_LT) && (op <= OP_LOR))
            type = VT::I32;
        else
            type = commonType(node(lhs).type, node(rhs).type);
        // The comparison type is the common type (kept in c as a VT for evaluation)
        int32_t n = makeNode(op, type, lhs, rhs, (int32_t)commonType(node(lhs).type, node(rhs).type));
        return n;
    }

    // Binary operators by precedence climbing (one function for every level, so nesting costs little stack): the
    // C precedence of an operator token, or 0 if the token isn't a binary operator
    static uint8_t binaryPrecedence(const Token& t, uint8_t& op)
    {
        static const struct { const char* punct; uint8_t prec; uint8_t op; } OPS[] = {
            {"||", 1, OP_LOR}, {"&&", 2, OP_LAND}, {"|", 3, OP_OR}, {"^", 4, OP_XOR}, {"&", 5, OP_AND},
            {"==", 6, OP_EQ}, {"!=", 6, OP_NE}, {"<", 7, OP_LT}, {"<=", 7, OP_LE}, {">", 7, OP_GT}, {">=", 7, OP_GE},
            {"<<", 8, OP_SHL}, {">>", 8, OP_SHR}, {"+", 9, OP_ADD}, {"-", 9, OP_SUB},
            {"*", 10, OP_MUL}, {"/", 10, OP_DIV}, {"%", 10, OP_MOD},
        };
        if (t.kind != Tok::Punct)
            return 0;
        for (const auto& entry : OPS)
        {
            if (t.text == entry.punct)
            {
                op = entry.op;
                return entry.prec;
            }
        }
        return 0;
    }

    // Operators of precedence minPrec and above, left associative
    int32_t binaryExpr(uint8_t minPrec)
    {
        int32_t lhs = unary();
        while ((lhs >= 0) && !_failed)
        {
            uint8_t op = 0;
            uint8_t prec = binaryPrecedence(peek(), op);
            if ((prec == 0) || (prec < minPrec))
                break;
            _idx++;
            int32_t rhs = binaryExpr(prec + 1);
            lhs = binaryNode(op, lhs, rhs);
        }
        return lhs;
    }

    int32_t logicalOr()
    {
        return binaryExpr(1);
    }

    int32_t unary()
    {
        if (++_depth > MAX_NESTING)
            return fail("nested too deeply");
        int32_t result = unaryInner();
        _depth--;
        return result;
    }

    int32_t unaryInner()
    {
        if (acceptPunct("-"))
        {
            int32_t operand = unary();
            return (operand < 0) ? -1 : makeNode(OP_NEG, node(operand).type, operand);
        }
        if (acceptPunct("+"))
        {
            int32_t operand = unary();
            return (operand < 0) ? -1 : makeNode(OP_POS, node(operand).type, operand);
        }
        if (acceptPunct("!"))
        {
            int32_t operand = unary();
            return (operand < 0) ? -1 : makeNode(OP_NOT, VT::I32, operand);
        }
        if (acceptPunct("~"))
        {
            int32_t operand = unary();
            if (operand < 0)
                return -1;
            if (isFloatVT(node(operand).type))
                return fail("~ on a float");
            return makeNode(OP_BNOT, node(operand).type, operand);
        }
        if (isPunct("++") || isPunct("--"))
        {
            bool inc = isPunct("++");
            _idx++;
            int32_t operand = unary();
            if (operand < 0)
                return -1;
            if (!isLvalue(operand))
                return fail("can't increment this");
            return makeNode(inc ? OP_PREINC : OP_PREDEC, node(operand).type, operand);
        }
        NumType castType;
        if (isPunct("(") && typeNameAt(1, castType) && isPunct(")", 2))
        {
            _idx += 3;
            int32_t operand = unary();
            if (operand < 0)
                return -1;
            Node n;
            n.op = OP_CAST;
            n.type = promotedType(castType);
            n.storeType = castType;
            n.a = operand;
            return addNode(n);
        }
        int32_t operand = primary();
        if (operand < 0)
            return -1;
        if (isPunct("++") || isPunct("--"))
        {
            bool inc = isPunct("++");
            _idx++;
            if (!isLvalue(operand))
                return fail("can't increment this");
            return makeNode(inc ? OP_POSTINC : OP_POSTDEC, node(operand).type, operand);
        }
        return operand;
    }

    int32_t literalNode(const Token& t)
    {
        Node n;
        n.op = OP_LIT;
        if (t.kind == Tok::NumFloat)
        {
            n.lit = makeF64(strtod(t.text.c_str(), nullptr));
        }
        else
        {
            bool isHex = (t.kind == Tok::NumHex);
            errno = 0;
            unsigned long long value = strtoull(isHex ? t.text.c_str() + 2 : t.text.c_str(), nullptr, isHex ? 16 : 10);
            if (errno == ERANGE)
                return fail("number too large");
            // The type of the C literal: decimal int or long long; hex int, unsigned int, long long or unsigned
            // long long
            if (value <= (unsigned long long)INT_MAX)
                n.lit = makeI32((int32_t)value);
            else if (isHex && (value <= (unsigned long long)UINT_MAX))
                n.lit = makeU32((uint32_t)value);
            else if (value <= (unsigned long long)LLONG_MAX)
                n.lit = makeI64((int64_t)value);
            else if (isHex)
                n.lit = makeU64(value);
            else
                return fail("number too large");
        }
        n.type = n.lit.t;
        return addNode(n);
    }

    int32_t primary()
    {
        const Token t = peek();
        if ((t.kind == Tok::NumInt) || (t.kind == Tok::NumHex) || (t.kind == Tok::NumFloat))
        {
            _idx++;
            return literalNode(t);
        }
        if (acceptPunct("("))
        {
            int32_t inner = expression();
            if ((inner < 0) || !expectPunct(")"))
                return -1;
            return inner;
        }
        if (t.kind != Tok::Id)
            return fail("expected a value");
        _idx++;
        if (t.text == "buf")
        {
            if (!expectPunct("["))
                return -1;
            int32_t index = expression();
            if ((index < 0) || !expectPunct("]"))
                return -1;
            if (isFloatVT(node(index).type))
                return fail("buf index is a float");
            return makeNode(OP_BUF, VT::I32, index);
        }
        if (t.text.startsWith("out."))
        {
            if (_prog._kind != DeviceTypeCodeProgram::Kind::Decode)
                return fail("init code assigns init.<name> values, not " + t.text);
            String name = t.text.substring(4);
            int32_t fieldIdx = -1;
            for (uint32_t i = 0; i < _prog._outFields.size(); i++)
                if (_prog._outFields[i].name == name)
                    fieldIdx = (int32_t)i;
            if (fieldIdx < 0)
                return fail(t.text + " is not an attribute of the record");
            const auto& field = _prog._outFields[fieldIdx];
            if (isPunct("["))
            {
                // An array element: only written, out.<name>[i] = value (as the build accepts)
                if (field.count <= 1)
                    return fail(t.text + " is not an array attribute");
                _idx++;
                int32_t index = expression();
                if ((index < 0) || !expectPunct("]"))
                    return -1;
                if (!isPunct("="))
                    return fail(t.text + "[...] must be a simple assignment");
                _idx++;
                int32_t value = assignment();
                if (value < 0)
                    return -1;
                if (isFloatVT(node(index).type))
                    return fail("array index is a float");
                return makeNode(OP_ARRAY_WRITE, VT::I32, fieldIdx, index, value);
            }
            if (field.count > 1)
                return fail(t.text + " is an array attribute - write out." + name + "[i] = value");
            Node n;
            n.op = OP_OUT;
            n.type = promotedType(field.type);
            n.storeType = field.type;
            n.a = fieldIdx;
            return addNode(n);
        }
        if (t.text.startsWith("init."))
        {
            if (_prog._kind != DeviceTypeCodeProgram::Kind::Init)
                return fail(t.text + " is only for init code");
            String name = t.text.substring(5);
            if ((name.length() == 0) || (name.indexOf('.') >= 0))
                return fail("invalid name " + t.text);
            int32_t valueIdx = -1;
            for (uint32_t i = 0; i < _prog._initNames.size(); i++)
                if (_prog._initNames[i] == name)
                    valueIdx = (int32_t)i;
            if (valueIdx < 0)
            {
                valueIdx = (int32_t)_prog._initNames.size();
                _prog._initNames.push_back(name);
            }
            Node n;
            n.op = OP_INIT;
            n.type = VT::I32;
            n.storeType = NumType::Int32;
            n.a = valueIdx;
            return addNode(n);
        }
        if (t.text.indexOf('.') >= 0)
            return fail(t.text + " is not allowed");
        int32_t slot = findVar(t.text);
        if (slot < 0)
            return fail(t.text + " is not declared");
        Node n;
        n.op = OP_VAR;
        n.storeType = _prog._slotTypes[slot];
        n.type = promotedType(n.storeType);
        n.a = slot;
        return addNode(n);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceTypeCodeProgram* DeviceTypeCodeProgram::compile(const char* pCode, Kind kind, const std::vector<OutField>& outFields,
            String& problem)
{
    std::vector<Token> tokens;
    if (!lex(pCode, tokens, problem))
        return nullptr;
    DeviceTypeCodeProgram* pProg = new (std::nothrow) DeviceTypeCodeProgram();
    if (!pProg)
    {
        problem = "out of memory";
        return nullptr;
    }
    pProg->_kind = kind;
    pProg->_outFields = outFields;
    DeviceTypeCodeCompiler compiler(*pProg, tokens);
    if (!compiler.compileProgram(problem))
    {
        delete pProg;
        return nullptr;
    }
    return pProg;
}

bool DeviceTypeCodeProgram::hasNext() const
{
    for (const Node& n : _nodes)
        if (n.op == OP_NEXT)
            return true;
    return false;
}

struct DeviceTypeCodeProgram::Exec
{
    RunContext& ctx;
    std::vector<Val> slots;
    bool outOfBudget = false;
    // Nesting of eval and exec calls (each is a stack frame), and the deepest reached
    uint32_t evalDepth = 0;
    uint32_t execDepth = 0;
    struct Depth
    {
        uint32_t& depth;
        uint32_t& maxDepth;
        Depth(uint32_t& d, uint32_t& m) : depth(d), maxDepth(m)
        {
            if (++depth > maxDepth)
                maxDepth = depth;
        }
        ~Depth()
        {
            depth--;
        }
    };
    bool spend()
    {
        if (ctx.budget == 0)
        {
            outOfBudget = true;
            return false;
        }
        ctx.budget--;
        return true;
    }
};

DeviceTypeCodeProgram::Status DeviceTypeCodeProgram::run(RunContext& ctx) const
{
    Exec ex{ctx, std::vector<Val>(_slotTypes.size())};
    Status status = exec(_root, ex);
    if (ex.outOfBudget)
        return Status::OutOfBudget;
    return status;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Evaluate an expression
DeviceTypeCodeProgram::Val DeviceTypeCodeProgram::eval(int32_t nodeIdx, Exec& ex) const
{
    Exec::Depth depth(ex.evalDepth, ex.ctx.maxEvalDepth);
    if (!ex.spend())
        return makeI32(0);
    const Node& n = _nodes[nodeIdx];
    switch (n.op)
    {
        case OP_LIT:
            return n.lit;
        case OP_VAR:
            return ex.slots[n.a];
        case OP_BUF:
        {
            Val indexVal = eval(n.a, ex);
            int64_t index = as<int64_t>(indexVal);
            if ((indexVal.t == VT::U64) && (indexVal.u > (uint64_t)INT64_MAX))
                index = -1;
            if (!ex.ctx.pBuf || (index < 0) || (index >= (int64_t)ex.ctx.bufLen))
                return makeI32(0);
            return makeI32(ex.ctx.pBuf[index]);
        }
        case OP_OUT:
            return ex.ctx.pOut ? loadField(ex.ctx.pOut + _outFields[n.a].offset, n.storeType) : makeI32(0);
        case OP_INIT:
            return ((uint32_t)n.a < ex.ctx.numInitValues) ? makeI32(ex.ctx.pInitValues[n.a]) : makeI32(0);
        case OP_CAST:
            return narrowTo(eval(n.a, ex), n.storeType);
        case OP_NEG:
        {
            Val v = eval(n.a, ex);
            switch (n.type)
            {
                case VT::I32: return makeI32((int32_t)(0u - (uint32_t)as<int32_t>(v)));
                case VT::U32: return makeU32(0u - as<uint32_t>(v));
                case VT::I64: return makeI64((int64_t)(0ull - (uint64_t)as<int64_t>(v)));
                case VT::U64: return makeU64(0ull - as<uint64_t>(v));
                case VT::F32: return makeF32(-as<float>(v));
                default: return makeF64(-as<double>(v));
            }
        }
        case OP_POS:
            return eval(n.a, ex);
        case OP_NOT:
            return makeI32(truthy(eval(n.a, ex)) ? 0 : 1);
        case OP_BNOT:
        {
            Val v = eval(n.a, ex);
            switch (n.type)
            {
                case VT::I32: return makeI32(~as<int32_t>(v));
                case VT::U32: return makeU32(~as<uint32_t>(v));
                case VT::I64: return makeI64(~as<int64_t>(v));
                default: return makeU64(~as<uint64_t>(v));
            }
        }
        case OP_LAND:
        {
            if (!truthy(eval(n.a, ex)))
                return makeI32(0);
            return makeI32(truthy(eval(n.b, ex)) ? 1 : 0);
        }
        case OP_LOR:
        {
            if (truthy(eval(n.a, ex)))
                return makeI32(1);
            return makeI32(truthy(eval(n.b, ex)) ? 1 : 0);
        }
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_AND: case OP_OR: case OP_XOR:
        case OP_SHL: case OP_SHR: case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_NE:
        {
            Val lhs = eval(n.a, ex);
            Val rhs = eval(n.b, ex);
            VT opType = ((n.op == OP_SHL) || (n.op == OP_SHR)) ? n.type : (VT)n.c;
            return binary(n.op, opType, lhs, rhs);
        }
        case OP_ASSIGN: case OP_PREINC: case OP_PREDEC: case OP_POSTINC: case OP_POSTDEC:
        {
            const Node& target = _nodes[n.a];
            Val old = eval(n.a, ex);
            Val result;
            if (n.op == OP_ASSIGN)
            {
                Val rhs = eval(n.b, ex);
                if (n.c < 0)
                {
                    result = rhs;
                }
                else
                {
                    VT opType = ((n.c == OP_SHL) || (n.c == OP_SHR)) ? old.t : commonType(old.t, rhs.t);
                    result = binary((uint8_t)n.c, opType, old, rhs);
                }
            }
            else
            {
                bool inc = (n.op == OP_PREINC) || (n.op == OP_POSTINC);
                Val one = makeI32(1);
                result = binary(inc ? OP_ADD : OP_SUB, commonType(old.t, VT::I32), old, one);
            }
            Val stored = narrowTo(result, target.storeType);
            switch (target.op)
            {
                case OP_VAR: ex.slots[target.a] = stored; break;
                case OP_OUT:
                    if (ex.ctx.pOut)
                        storeField(ex.ctx.pOut + _outFields[target.a].offset, target.storeType, stored);
                    break;
                case OP_INIT:
                    if ((uint32_t)target.a < ex.ctx.numInitValues)
                        ex.ctx.pInitValues[target.a] = as<int32_t>(stored);
                    break;
                default: break;
            }
            return ((n.op == OP_POSTINC) || (n.op == OP_POSTDEC)) ? old : stored;
        }
        case OP_ARRAY_WRITE:
        {
            // { int __ai = (index); if ((__ai >= 0) && (__ai < count)) pOut->name[__ai] = (value); }
            int32_t index = as<int32_t>(eval(n.b, ex));
            const OutField& field = _outFields[n.a];
            if ((index >= 0) && (index < (int32_t)field.count))
            {
                Val v = eval(n.c, ex);
                if (ex.ctx.pOut)
                    storeField(ex.ctx.pOut + field.offset + (uint32_t)index * storeSize(field.type), field.type, v);
            }
            return makeI32(0);
        }
        default:
            return makeI32(0);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Execute a statement
DeviceTypeCodeProgram::Status DeviceTypeCodeProgram::exec(int32_t nodeIdx, Exec& ex) const
{
    Exec::Depth depth(ex.execDepth, ex.ctx.maxExecDepth);
    if (!ex.spend())
        return Status::OutOfBudget;
    const Node& n = _nodes[nodeIdx];
    switch (n.op)
    {
        case OP_BLOCK:
        {
            for (int32_t i = 0; i < n.b; i++)
            {
                Status status = exec(_lists[n.a + i], ex);
                if (ex.outOfBudget)
                    return Status::OutOfBudget;
                if (status != Status::Done)
                    return status;
            }
            return Status::Done;
        }
        case OP_DECL:
            ex.slots[n.a] = (n.b >= 0) ? narrowTo(eval(n.b, ex), n.storeType) : narrowTo(makeI32(0), n.storeType);
            return ex.outOfBudget ? Status::OutOfBudget : Status::Done;
        case OP_EXPR:
            eval(n.a, ex);
            return ex.outOfBudget ? Status::OutOfBudget : Status::Done;
        case OP_IF:
        {
            bool cond = truthy(eval(n.a, ex));
            if (ex.outOfBudget)
                return Status::OutOfBudget;
            if (cond)
                return exec(n.b, ex);
            if (n.c >= 0)
                return exec(n.c, ex);
            return Status::Done;
        }
        case OP_WHILE:
        {
            while (true)
            {
                bool cond = truthy(eval(n.a, ex));
                if (ex.outOfBudget)
                    return Status::OutOfBudget;
                if (!cond)
                    return Status::Done;
                Status status = exec(n.b, ex);
                if (status == Status::Break)
                    return Status::Done;        // the break of next leaves the innermost while
                if (status != Status::Done)
                    return status;
            }
        }
        case OP_RETURN:
        {
            Val v = (n.a >= 0) ? eval(n.a, ex) : makeI32(0);
            if (ex.outOfBudget)
                return Status::OutOfBudget;
            // The generated function returns uint32_t (decode) or bool (init)
            ex.ctx.returnValue = (_kind == Kind::Decode) ? (int64_t)as<uint32_t>(v) : (as<bool>(v) ? 1 : 0);
            return Status::Return;
        }
        case OP_NEXT:
            if (!ex.ctx.nextFn || !ex.ctx.nextFn(ex.ctx.pNextContext))
                return Status::Break;
            return Status::Done;
        default:
            return Status::Done;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime init
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include <atomic>
#include <utility>
#include "DeviceTypeGrammar.h"

namespace
{
    // Instructions init code may run
    const uint32_t INIT_CODE_BUDGET = 100000;

    std::atomic<const DeviceTypeRuntimeInit*> g_initSlots[RAFT_RUNTIME_INIT_MAX];

    template <size_t SLOT>
    bool slotInit(const uint8_t* pReadBuf, uint32_t readBufLen, int32_t* pValues, uint32_t numValues)
    {
        const DeviceTypeRuntimeInit* pInit = g_initSlots[SLOT].load(std::memory_order_acquire);
        return pInit ? pInit->run(pReadBuf, readBufLen, pValues, numValues) : false;
    }

    template <size_t... SLOTS>
    constexpr std::array<DeviceTypeRecordInitFn, sizeof...(SLOTS)> makeInitSlotFns(std::index_sequence<SLOTS...>)
    {
        return {{&slotInit<SLOTS>...}};
    }
    const std::array<DeviceTypeRecordInitFn, RAFT_RUNTIME_INIT_MAX> g_initSlotFns =
                makeInitSlotFns(std::make_index_sequence<RAFT_RUNTIME_INIT_MAX>{});
}

DeviceTypeRuntimeInit* DeviceTypeRuntimeInit::create(const char* pInitCode, const char* pInitValues, String& problem, bool takeSlot)
{
    DeviceTypeRuntimeInit* pInit = new (std::nothrow) DeviceTypeRuntimeInit();
    if (!pInit)
    {
        problem = "out of memory";
        return nullptr;
    }
    pInit->_pProgram = DeviceTypeCodeProgram::compile(pInitCode, DeviceTypeCodeProgram::Kind::Init, {}, problem);
    if (!pInit->_pProgram)
    {
        problem = "initCode: " + problem;
        delete pInit;
        return nullptr;
    }

    // The bytes read by the init steps before the first step with a value slot or guard (ProcessDevTypeJsonToC)
    DeviceTypeGrammar::Result steps = DeviceTypeGrammar::compile(pInitValues, DeviceTypeGrammar::Field::Init);
    for (const DeviceTypeGrammar::Step& step : steps.steps)
    {
        if (!step.slots.empty() || !step.guard.empty())
            break;
        pInit->_requiredReadBytes += step.readLen;
    }
    for (const String& name : pInit->_pProgram->getInitValueNames())
        pInit->_namesStr += String(pInit->_namesStr.length() ? "," : "") + name;

    if (!takeSlot)
        return pInit;
    for (uint32_t slot = 0; slot < RAFT_RUNTIME_INIT_MAX; slot++)
    {
        const DeviceTypeRuntimeInit* pFree = nullptr;
        if (g_initSlots[slot].compare_exchange_strong(pFree, pInit, std::memory_order_acq_rel))
        {
            pInit->_slot = (int32_t)slot;
            pInit->_initFn = g_initSlotFns[slot];
            return pInit;
        }
    }
    problem = "no runtime init slot free (RAFT_RUNTIME_INIT_MAX " + String(RAFT_RUNTIME_INIT_MAX) + ")";
    delete pInit;
    return nullptr;
}

DeviceTypeRuntimeInit::~DeviceTypeRuntimeInit()
{
    if ((_slot >= 0) && (_slot < RAFT_RUNTIME_INIT_MAX))
        g_initSlots[_slot].store(nullptr, std::memory_order_release);
    delete _pProgram;
}

const std::vector<String>& DeviceTypeRuntimeInit::getNames() const
{
    return _pProgram->getInitValueNames();
}

bool DeviceTypeRuntimeInit::run(const uint8_t* pReadBuf, uint32_t readBufLen, int32_t* pValues, uint32_t numValues) const
{
    // if ((readBufLen < N) || (numValues < K)) return false; <code> return true;
    if ((readBufLen < _requiredReadBytes) || (numValues < _pProgram->getInitValueNames().size()))
        return false;
    DeviceTypeCodeProgram::RunContext ctx;
    ctx.pBuf = pReadBuf;
    ctx.bufLen = readBufLen;
    ctx.pInitValues = pValues;
    ctx.numInitValues = numValues;
    ctx.budget = INIT_CODE_BUDGET;
    DeviceTypeCodeProgram::Status status = _pProgram->run(ctx);
    if (status == DeviceTypeCodeProgram::Status::Return)
        return ctx.returnValue != 0;
    return status == DeviceTypeCodeProgram::Status::Done;
}

uint32_t DeviceTypeRuntimeInit::getNumSlotsInUse()
{
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < RAFT_RUNTIME_INIT_MAX; slot++)
        if (g_initSlots[slot].load(std::memory_order_acquire))
            count++;
    return count;
}

#else // RAFT_RUNTIME_DECODE_CODE

DeviceTypeCodeProgram* DeviceTypeCodeProgram::compile(const char*, Kind, const std::vector<OutField>&, String& problem)
{
    problem = "the code interpreter isn't built in (RAFT_RUNTIME_DECODE_CODE)";
    return nullptr;
}

DeviceTypeCodeProgram::Status DeviceTypeCodeProgram::run(RunContext&) const
{
    return Status::Done;
}

bool DeviceTypeCodeProgram::hasNext() const
{
    return false;
}

DeviceTypeRuntimeInit* DeviceTypeRuntimeInit::create(const char*, const char*, String& problem, bool)
{
    problem = "the code interpreter isn't built in (RAFT_RUNTIME_DECODE_CODE)";
    return nullptr;
}
DeviceTypeRuntimeInit::~DeviceTypeRuntimeInit()
{
}
const std::vector<String>& DeviceTypeRuntimeInit::getNames() const
{
    static const std::vector<String> NONE;
    return NONE;
}
bool DeviceTypeRuntimeInit::run(const uint8_t*, uint32_t, int32_t*, uint32_t) const
{
    return false;
}
uint32_t DeviceTypeRuntimeInit::getNumSlotsInUse()
{
    return 0;
}

#endif // RAFT_RUNTIME_DECODE_CODE
