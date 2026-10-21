/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeCodeInterpreter.cpp
//
// See DeviceTypeCodeInterpreter.h
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeCodeInterpreter.h"

#ifdef RAFT_RUNTIME_DECODE_CODE

#include <alloca.h>
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

// The program as bytecode, if it is within the bytecode's subset (below)
bool buildBytecode(DeviceTypeCodeProgram& prog);

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
    // Also as bytecode, if it is within the bytecode's subset
    pProg->_bytecodeOk = buildBytecode(*pProg);
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
    if (_bytecodeOk && s_bytecodeEnabled)
        return runBytecode(ctx);
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
// Bytecode: the typed tree as a flat list of instructions, run by one loop - no recursion, a fixed operand stack and
// variable slots, no allocation. Every instruction's types are known when it is built, from the types the tree's
// values have when it runs, so an instruction does what the tree's node does:
// - 32-bit int values (most decode code) have their own instructions, with combined ones for the patterns decode code
//   is made of: buf[var + const], operations with a constant, var += const, compare and branch
// - other types use the tree's own conversions and operations (as<T>, narrowTo, binary, loadField, storeField)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceTypeCodeProgram::s_bytecodeEnabled = true;

namespace
{
    using Cell = DeviceTypeCodeProgram::Cell;

    enum BcOp : uint8_t
    {
        // 32-bit int values
        BC_PUSH_K,          // push b
        BC_LOAD_VAR,        // push slot a
        BC_STORE_VAR,       // pop to slot a
        BC_LOAD_BUF,        // pop index, push buf[index] (0 outside the data)
        BC_LOAD_BUF_VAR_K,  // push buf[slot a + b]
        BC_LOAD_BUF_K,      // push buf[b]
        BC_STORE_OUT,       // pop a value of type a to the output field at offset b, of storage type t
        BC_NARROW,          // an int to storage type t and back (int8_t, uint8_t, int16_t, uint16_t, int32_t, bool)
        BC_DUP, BC_POP,
        // In the order of the tree's operations (OP_ADD ... OP_XOR, OP_LT ... OP_NE): BC_ADD + (op - OP_ADD)
        BC_ADD, BC_SUB, BC_MUL, BC_DIV, BC_MOD, BC_SHL, BC_SHR, BC_AND, BC_OR, BC_XOR,
        BC_LT, BC_LE, BC_GT, BC_GE, BC_EQ, BC_NE,
        BC_ADD_K, BC_MUL_K, BC_AND_K, BC_OR_K, BC_XOR_K, BC_SHL_K, BC_SHR_K,
        BC_NEG, BC_NOT, BC_BNOT, BC_BOOL,
        BC_ADD_VAR_K,       // slot a = narrow(slot a + b) to storage type t
        BC_JMP,             // to b
        BC_JZ, BC_JNZ,      // pop, jump to b if zero / not zero
        BC_JGE, BC_JGT, BC_JLE, BC_JLT, BC_JNE, BC_JEQ,     // pop two, jump to b if (lhs op rhs)
        BC_NEXT,            // next: if the output is full, jump to b (the end of the innermost while) or end (Break)
        BC_RET,             // pop a value of type a and return it (t: 0 decode - as uint32_t, 1 init - as bool)
        BC_HALT,
        // Any type
        BC_PUSH_C,          // push constant b
        BC_CONV,            // a value of type a to type t (the C++ conversion)
        BC_NARROW_V,        // a value of type a to storage type t (narrowTo)
        BC_BIN,             // operation a on type t (the operands of type t; a shift's count an int64_t)
        BC_UN,              // OP_NEG or OP_BNOT (a) on type t
        BC_TRUTHY,          // a value of type t to 0 or 1
        BC_LOAD_BUF_V,      // pop an index of type t, push buf[index]
        BC_LOAD_OUT,        // push the output field at offset b, storage type t
        BC_LOAD_INIT,       // push init value a
        BC_STORE_INIT,      // pop to init value a
        BC_ARR_CHECK,       // the int index on the stack outside 0 .. a-1: pop it and jump to b
        BC_ARR_STORE,       // pop a value of type a and an index: to the element of the field at offset b, storage type t
    };

    const uint32_t BC_MAX_STACK = 16;
    const uint32_t BC_MAX_SLOTS = 64;       // variables (each program's are on the stack, as many as it has)

    inline int32_t wrapAdd32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
    inline int32_t wrapSub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
    inline int32_t wrapMul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
    inline int32_t div32(int32_t a, int32_t b) { return (b == 0) ? 0 : (b == -1) ? wrapSub32(0, a) : a / b; }
    inline int32_t mod32(int32_t a, int32_t b) { return ((b == 0) || (b == -1)) ? 0 : a % b; }
    inline int32_t shl32(int32_t a, int32_t n) { return (int32_t)((uint32_t)a << ((uint32_t)n & 31)); }
    inline int32_t shr32(int32_t a, int32_t n) { return a >> ((uint32_t)n & 31); }

    // An int converted to an int storage type and promoted back (narrowTo for an int)
    inline int32_t narrow32(int32_t v, uint8_t st)
    {
        switch ((NumType)st)
        {
            case NumType::Int8: return (int8_t)v;
            case NumType::Uint8: return (uint8_t)v;
            case NumType::Int16: return (int16_t)v;
            case NumType::Uint16: return (uint16_t)v;
            case NumType::Bool: return v != 0;
            default: return v;
        }
    }

    // A storage type whose values are ints
    bool isIntStore(NumType st)
    {
        return (st == NumType::Int8) || (st == NumType::Uint8) || (st == NumType::Int16) || (st == NumType::Uint16) ||
                    (st == NumType::Int32) || (st == NumType::Bool);
    }

    // A cell as a value of type t, and a value as a cell
    inline Val cellToVal(const Cell& c, VT t)
    {
        switch (t)
        {
            case VT::I32: return makeI32(c.i32);
            case VT::U32: return makeU32(c.u32);
            case VT::I64: return makeI64(c.i64);
            case VT::U64: return makeU64(c.u64);
            case VT::F32: return makeF32(c.f);
            default: return makeF64(c.d);
        }
    }
    inline Cell valToCell(const Val& v)
    {
        Cell c;
        c.u64 = 0;
        switch (v.t)
        {
            case VT::I32: c.i32 = (int32_t)v.i; break;
            case VT::U32: c.u32 = (uint32_t)v.i; break;
            case VT::I64: c.i64 = v.i; break;
            case VT::U64: c.u64 = v.u; break;
            case VT::F32: c.f = v.f; break;
            default: c.d = v.d; break;
        }
        return c;
    }

    // The C++ conversion of a value to type t
    Val convertTo(const Val& v, VT t)
    {
        switch (t)
        {
            case VT::I32: return makeI32(as<int32_t>(v));
            case VT::U32: return makeU32(as<uint32_t>(v));
            case VT::I64: return makeI64(as<int64_t>(v));
            case VT::U64: return makeU64(as<uint64_t>(v));
            case VT::F32: return makeF32(as<float>(v));
            default: return makeF64(as<double>(v));
        }
    }

    // OP_NEG and OP_BNOT on type t (as eval)
    Val unary(uint8_t op, VT t, const Val& v)
    {
        if (op == OP_NEG)
        {
            switch (t)
            {
                case VT::I32: return makeI32((int32_t)(0u - (uint32_t)as<int32_t>(v)));
                case VT::U32: return makeU32(0u - as<uint32_t>(v));
                case VT::I64: return makeI64((int64_t)(0ull - (uint64_t)as<int64_t>(v)));
                case VT::U64: return makeU64(0ull - as<uint64_t>(v));
                case VT::F32: return makeF32(-as<float>(v));
                default: return makeF64(-as<double>(v));
            }
        }
        switch (t)
        {
            case VT::I32: return makeI32(~as<int32_t>(v));
            case VT::U32: return makeU32(~as<uint32_t>(v));
            case VT::I64: return makeI64(~as<int64_t>(v));
            default: return makeU64(~as<uint64_t>(v));
        }
    }

    // The type of a shift's result (as binary)
    VT shiftResultType(VT t)
    {
        return ((t == VT::I32) || (t == VT::U32) || (t == VT::I64)) ? t : VT::U64;
    }
}

class DeviceTypeBytecodeBuilder
{
public:
    explicit DeviceTypeBytecodeBuilder(DeviceTypeCodeProgram& prog) : _prog(prog) {}

    bool build()
    {
        if (_prog._slotTypes.size() > BC_MAX_SLOTS)
            return false;
        if (!stmt(_prog._root))
            return false;
        emit(BC_HALT);
        _prog._code = std::move(_code);
        _prog._consts = std::move(_consts);
        return true;
    }

private:
    using Ins = DeviceTypeCodeProgram::Ins;
    DeviceTypeCodeProgram& _prog;
    std::vector<Ins> _code;
    std::vector<Cell> _consts;
    uint32_t _depth = 0;
    std::vector<std::vector<uint32_t>> _whileExits;     // NEXT instructions to point at each enclosing while's end

    const Node& node(int32_t idx) const
    {
        return _prog._nodes[idx];
    }

    uint32_t emit(uint8_t op, uint8_t t = 0, int32_t a = 0, int32_t b = 0)
    {
        Ins ins;
        ins.op = op;
        ins.t = t;
        ins.a = (int16_t)a;
        ins.b = b;
        _code.push_back(ins);
        return (uint32_t)_code.size() - 1;
    }

    bool push()
    {
        _depth++;
        return _depth <= BC_MAX_STACK;
    }
    void pop(uint32_t n = 1)
    {
        _depth -= n;
    }
    uint32_t here() const
    {
        return (uint32_t)_code.size();
    }
    void patch(uint32_t insIdx, uint32_t target)
    {
        _code[insIdx].b = (int32_t)target;
    }

    static bool isI32Lit(const Node& n)
    {
        return (n.op == OP_LIT) && (n.lit.t == VT::I32);
    }
    static int32_t litValue(const Node& n)
    {
        return (int32_t)n.lit.i;
    }

    // The type of the value the tree gives for a node when it runs
    VT typeOf(int32_t idx) const
    {
        const Node& n = node(idx);
        switch (n.op)
        {
            case OP_LIT: return n.lit.t;
            case OP_VAR: return promotedType(_prog._slotTypes[n.a]);
            case OP_BUF: case OP_INIT: case OP_NOT: case OP_LAND: case OP_LOR: case OP_ARRAY_WRITE:
            case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_NE:
                return VT::I32;
            case OP_OUT: case OP_CAST: return promotedType(n.storeType);
            case OP_NEG: case OP_BNOT: return n.type;
            case OP_POS: return typeOf(n.a);
            case OP_SHL: case OP_SHR: return shiftResultType(n.type);
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_AND: case OP_OR: case OP_XOR:
                return (VT)n.c;
            case OP_ASSIGN: case OP_PREINC: case OP_PREDEC: return promotedType(node(n.a).storeType);
            case OP_POSTINC: case OP_POSTDEC: return typeOf(n.a);
            default: return VT::I32;
        }
    }

    // A value of type from as type to (nothing if the same)
    void conv(VT from, VT to)
    {
        if (from != to)
            emit(BC_CONV, (uint8_t)to, (int32_t)from);
    }

    // A value of type from to a storage type (narrowTo): the value is then of the storage type's promoted type
    void narrowStore(VT from, NumType st)
    {
        if ((from == VT::I32) && isIntStore(st))
        {
            if (st != NumType::Int32)
                emit(BC_NARROW, (uint8_t)st);
            return;
        }
        emit(BC_NARROW_V, (uint8_t)st, (int32_t)from);
    }

    // An int (0 or not) from a value of type t
    void toTruth(VT t)
    {
        if (t != VT::I32)
            emit(BC_TRUTHY, (uint8_t)t);
    }

    bool pushConst(const Val& v)
    {
        if (!push())
            return false;
        if (v.t == VT::I32)
        {
            emit(BC_PUSH_K, 0, 0, (int32_t)v.i);
            return true;
        }
        _consts.push_back(valToCell(v));
        emit(BC_PUSH_C, 0, 0, (int32_t)_consts.size() - 1);
        return true;
    }

    // Statements
    bool stmt(int32_t idx)
    {
        const Node& n = node(idx);
        switch (n.op)
        {
            case OP_BLOCK:
                for (int32_t i = 0; i < n.b; i++)
                    if (!stmt(_prog._lists[n.a + i]))
                        return false;
                return true;
            case OP_EMPTY:
                return true;
            case OP_DECL:
            {
                VT t = VT::I32;
                if (n.b >= 0)
                {
                    if (!expr(n.b, true, t))
                        return false;
                }
                else if (!pushConst(makeI32(0)))
                {
                    return false;
                }
                narrowStore(t, n.storeType);
                emit(BC_STORE_VAR, 0, n.a);
                pop();
                return true;
            }
            case OP_EXPR:
            {
                VT t;
                return expr(n.a, false, t);
            }
            case OP_IF:
            {
                std::vector<uint32_t> toElse;
                if (!cond(n.a, toElse))
                    return false;
                if (!stmt(n.b))
                    return false;
                if (n.c < 0)
                {
                    for (uint32_t j : toElse)
                        patch(j, here());
                    return true;
                }
                uint32_t toEnd = emit(BC_JMP);
                for (uint32_t j : toElse)
                    patch(j, here());
                if (!stmt(n.c))
                    return false;
                patch(toEnd, here());
                return true;
            }
            case OP_WHILE:
            {
                uint32_t start = here();
                std::vector<uint32_t> toEnd;
                if (!cond(n.a, toEnd))
                    return false;
                _whileExits.emplace_back();
                if (!stmt(n.b))
                    return false;
                emit(BC_JMP, 0, 0, (int32_t)start);
                for (uint32_t j : toEnd)
                    patch(j, here());
                for (uint32_t j : _whileExits.back())
                    patch(j, here());
                _whileExits.pop_back();
                return true;
            }
            case OP_NEXT:
            {
                uint32_t j = emit(BC_NEXT, 0, 0, -1);
                if (!_whileExits.empty())
                    _whileExits.back().push_back(j);
                return true;
            }
            case OP_RETURN:
            {
                VT t = VT::I32;
                if (n.a >= 0)
                {
                    if (!expr(n.a, true, t))
                        return false;
                }
                else if (!pushConst(makeI32(0)))
                {
                    return false;
                }
                emit(BC_RET, (_prog._kind == DeviceTypeCodeProgram::Kind::Decode) ? 0 : 1, (int32_t)t);
                pop();
                return true;
            }
            default:
                return false;
        }
    }

    // A condition: jumps added to toFalse are taken when it is false (patched by the caller)
    bool cond(int32_t idx, std::vector<uint32_t>& toFalse)
    {
        const Node& n = node(idx);
        if ((n.op >= OP_LT) && (n.op <= OP_NE) && ((VT)n.c == VT::I32))
        {
            VT ta, tb;
            if (!expr(n.a, true, ta))
                return false;
            conv(ta, VT::I32);
            if (!expr(n.b, true, tb))
                return false;
            conv(tb, VT::I32);
            static const uint8_t INVERSE[] = {BC_JGE, BC_JGT, BC_JLE, BC_JLT, BC_JNE, BC_JEQ};     // LT LE GT GE EQ NE
            toFalse.push_back(emit(INVERSE[n.op - OP_LT]));
            pop(2);
            return true;
        }
        if (n.op == OP_LAND)
            return cond(n.a, toFalse) && cond(n.b, toFalse);
        VT t;
        if (!expr(idx, true, t))
            return false;
        toTruth(t);
        toFalse.push_back(emit(BC_JZ));
        pop();
        return true;
    }

    // An expression: leaves its value (of type type) on the stack if want
    bool expr(int32_t idx, bool want, VT& type)
    {
        if (!exprInner(idx, want, type))
            return false;
        // The value's type is the one the tree gives
        return !want || (type == typeOf(idx));
    }

    bool exprInner(int32_t idx, bool want, VT& type)
    {
        const Node& n = node(idx);
        type = typeOf(idx);
        switch (n.op)
        {
            case OP_LIT:
                return !want || pushConst(n.lit);
            case OP_VAR:
                if (!want)
                    return true;
                if (!push())
                    return false;
                emit(BC_LOAD_VAR, 0, n.a);
                return true;
            case OP_OUT:
                if (!want)
                    return true;
                if (!push())
                    return false;
                emit(BC_LOAD_OUT, (uint8_t)n.storeType, 0, (int32_t)_prog._outFields[n.a].offset);
                return true;
            case OP_INIT:
                if (!want)
                    return true;
                if (!push())
                    return false;
                emit(BC_LOAD_INIT, 0, n.a);
                return true;
            case OP_BUF:
            {
                const Node& index = node(n.a);
                if ((index.op == OP_VAR) && (typeOf(n.a) == VT::I32))
                {
                    if (!push())
                        return false;
                    emit(BC_LOAD_BUF_VAR_K, 0, index.a, 0);
                }
                else if (isI32Lit(index))
                {
                    if (!push())
                        return false;
                    emit(BC_LOAD_BUF_K, 0, 0, litValue(index));
                }
                else if ((index.op == OP_ADD) && ((VT)index.c == VT::I32) && (node(index.a).op == OP_VAR) &&
                            (typeOf(index.a) == VT::I32) && isI32Lit(node(index.b)))
                {
                    if (!push())
                        return false;
                    emit(BC_LOAD_BUF_VAR_K, 0, node(index.a).a, litValue(node(index.b)));
                }
                else
                {
                    VT it;
                    if (!expr(n.a, true, it))
                        return false;
                    if (it == VT::I32)
                        emit(BC_LOAD_BUF);
                    else
                        emit(BC_LOAD_BUF_V, (uint8_t)it);
                }
                return discardIfUnwanted(want);
            }
            case OP_CAST:
            {
                VT t;
                if (!expr(n.a, true, t))
                    return false;
                narrowStore(t, n.storeType);
                return discardIfUnwanted(want);
            }
            case OP_POS:
                return expr(n.a, want, type);
            case OP_NEG: case OP_BNOT:
            {
                VT t;
                if (!expr(n.a, true, t))
                    return false;
                conv(t, n.type);
                if (n.type == VT::I32)
                    emit((n.op == OP_NEG) ? BC_NEG : BC_BNOT);
                else
                    emit(BC_UN, (uint8_t)n.type, n.op);
                return discardIfUnwanted(want);
            }
            case OP_NOT:
            {
                VT t;
                if (!expr(n.a, true, t))
                    return false;
                toTruth(t);
                emit(BC_NOT);
                return discardIfUnwanted(want);
            }
            case OP_LAND: case OP_LOR:
            {
                // a && b: a false -> 0, else (b != 0); a || b: a true -> 1, else (b != 0)
                VT t;
                if (!expr(n.a, true, t))
                    return false;
                toTruth(t);
                uint32_t shortCut = emit((n.op == OP_LAND) ? BC_JZ : BC_JNZ);
                pop();
                if (!expr(n.b, true, t))
                    return false;
                toTruth(t);
                emit(BC_BOOL);
                uint32_t toEnd = emit(BC_JMP);
                pop();
                patch(shortCut, here());
                emit(BC_PUSH_K, 0, 0, (n.op == OP_LAND) ? 0 : 1);
                if (!push())
                    return false;
                patch(toEnd, here());
                return discardIfUnwanted(want);
            }
            case OP_SHL: case OP_SHR:
            {
                // The left operand's type; the count as an int64_t (as binary)
                VT t = n.type;
                VT ta, tb;
                if (!expr(n.a, true, ta))
                    return false;
                conv(ta, t);
                if ((t == VT::I32) && isI32Lit(node(n.b)))
                {
                    emit((n.op == OP_SHL) ? BC_SHL_K : BC_SHR_K, 0, 0, litValue(node(n.b)));
                    return discardIfUnwanted(want);
                }
                if (!expr(n.b, true, tb))
                    return false;
                if ((t == VT::I32) && (tb == VT::I32))
                {
                    emit((n.op == OP_SHL) ? BC_SHL : BC_SHR);
                }
                else
                {
                    conv(tb, VT::I64);
                    emit(BC_BIN, (uint8_t)t, n.op);
                }
                pop();
                return discardIfUnwanted(want);
            }
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_AND: case OP_OR: case OP_XOR:
            case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_NE:
            {
                // Both operands of the common type
                VT opType = (VT)n.c;
                VT ta, tb;
                if (!expr(n.a, true, ta))
                    return false;
                conv(ta, opType);
                if ((opType == VT::I32) && isI32Lit(node(n.b)))
                {
                    int32_t k = litValue(node(n.b));
                    int kOp = -1;
                    switch (n.op)
                    {
                        case OP_ADD: kOp = BC_ADD_K; break;
                        case OP_SUB: kOp = BC_ADD_K; k = wrapSub32(0, k); break;
                        case OP_MUL: kOp = BC_MUL_K; break;
                        case OP_AND: kOp = BC_AND_K; break;
                        case OP_OR: kOp = BC_OR_K; break;
                        case OP_XOR: kOp = BC_XOR_K; break;
                        default: break;
                    }
                    if (kOp >= 0)
                    {
                        emit((uint8_t)kOp, 0, 0, k);
                        return discardIfUnwanted(want);
                    }
                }
                if (!expr(n.b, true, tb))
                    return false;
                conv(tb, opType);
                if (opType == VT::I32)
                    emit((uint8_t)(BC_ADD + (n.op - OP_ADD)));
                else
                    emit(BC_BIN, (uint8_t)opType, n.op);
                pop();
                return discardIfUnwanted(want);
            }
            case OP_ASSIGN: case OP_PREINC: case OP_PREDEC: case OP_POSTINC: case OP_POSTDEC:
                return assign(n, want);
            case OP_ARRAY_WRITE:
            {
                // { int __ai = (index); if ((__ai >= 0) && (__ai < count)) pOut->name[__ai] = (value); }
                const DeviceTypeCodeProgram::OutField& field = _prog._outFields[n.a];
                VT it, vt;
                if (!expr(n.b, true, it))
                    return false;
                conv(it, VT::I32);
                uint32_t outside = emit(BC_ARR_CHECK, 0, (int32_t)field.count);
                if (!expr(n.c, true, vt))
                    return false;
                emit(BC_ARR_STORE, (uint8_t)field.type, (int32_t)vt, (int32_t)field.offset);
                pop(2);
                patch(outside, here());
                return !want || pushConst(makeI32(0));
            }
            default:
                return false;
        }
    }

    bool discardIfUnwanted(bool want)
    {
        if (!want)
        {
            emit(BC_POP);
            pop();
        }
        return true;
    }

    // Push the current value of an assignment's target
    bool loadTarget(const Node& target)
    {
        if (!push())
            return false;
        if (target.op == OP_VAR)
            emit(BC_LOAD_VAR, 0, target.a);
        else if (target.op == OP_OUT)
            emit(BC_LOAD_OUT, (uint8_t)target.storeType, 0, (int32_t)_prog._outFields[target.a].offset);
        else
            emit(BC_LOAD_INIT, 0, target.a);
        return true;
    }

    // Pop a value (of the target's promoted type, or for an output field any type) to an assignment's target
    void storeTarget(const Node& target, VT valueType)
    {
        if (target.op == OP_VAR)
            emit(BC_STORE_VAR, 0, target.a);
        else if (target.op == OP_OUT)
            emit(BC_STORE_OUT, (uint8_t)target.storeType, (int32_t)valueType, (int32_t)_prog._outFields[target.a].offset);
        else
            emit(BC_STORE_INIT, 0, target.a);
        pop();
    }

    bool assign(const Node& n, bool want)
    {
        const Node& target = node(n.a);
        if ((target.op != OP_VAR) && (target.op != OP_OUT) && (target.op != OP_INIT))
            return false;
        VT oldType = typeOf(n.a);
        VT storedType = promotedType(target.storeType);
        bool isInc = (n.op != OP_ASSIGN);

        // ++, -- and int var += const with the value unused: one instruction
        if ((target.op == OP_VAR) && !want && isIntStore(target.storeType) &&
                    (isInc || (((n.c == OP_ADD) || (n.c == OP_SUB)) && isI32Lit(node(n.b)))))
        {
            int32_t k = 1;
            if (!isInc)
                k = (n.c == OP_ADD) ? litValue(node(n.b)) : wrapSub32(0, litValue(node(n.b)));
            else if ((n.op == OP_PREDEC) || (n.op == OP_POSTDEC))
                k = -1;
            emit(BC_ADD_VAR_K, (uint8_t)target.storeType, target.a, k);
            return true;
        }

        // ++ and --: the old value plus or minus one, in the common type of the target's and int
        if (isInc)
        {
            bool post = (n.op == OP_POSTINC) || (n.op == OP_POSTDEC);
            bool inc = (n.op == OP_PREINC) || (n.op == OP_POSTINC);
            if (!loadTarget(target))
                return false;
            if (post && want)
            {
                if (!push())
                    return false;
                emit(BC_DUP);
            }
            VT opType = commonType(oldType, VT::I32);
            conv(oldType, opType);
            if (opType == VT::I32)
            {
                emit(BC_ADD_K, 0, 0, inc ? 1 : -1);
            }
            else
            {
                if (!pushConst(convertTo(makeI32(1), opType)))
                    return false;
                emit(BC_BIN, (uint8_t)opType, inc ? OP_ADD : OP_SUB);
                pop();
            }
            narrowStore(opType, target.storeType);
            if (!post && want)
            {
                if (!push())
                    return false;
                emit(BC_DUP);
            }
            storeTarget(target, storedType);
            return true;
        }

        // Assignment: the value (a compound assignment's operation on the old value)
        VT resultType;
        if (n.c >= 0)
        {
            if (!loadTarget(target))
                return false;
            VT tb;
            if ((n.c == OP_SHL) || (n.c == OP_SHR))
            {
                // The old value's type; the count as an int64_t
                if (!expr(n.b, true, tb))
                    return false;
                if ((oldType == VT::I32) && (tb == VT::I32))
                {
                    emit((n.c == OP_SHL) ? BC_SHL : BC_SHR);
                }
                else
                {
                    conv(tb, VT::I64);
                    emit(BC_BIN, (uint8_t)oldType, n.c);
                }
                resultType = shiftResultType(oldType);
            }
            else
            {
                VT opType = commonType(oldType, typeOf(n.b));
                conv(oldType, opType);
                if (!expr(n.b, true, tb))
                    return false;
                conv(tb, opType);
                if (opType == VT::I32)
                    emit((uint8_t)(BC_ADD + (n.c - OP_ADD)));
                else
                    emit(BC_BIN, (uint8_t)opType, n.c);
                resultType = opType;
            }
            pop();
        }
        else if (!expr(n.b, true, resultType))
        {
            return false;
        }

        // An output field written as a statement takes the value as it is (storeField converts it as narrowTo does)
        if ((target.op == OP_OUT) && !want)
        {
            storeTarget(target, resultType);
            return true;
        }
        narrowStore(resultType, target.storeType);
        if (want)
        {
            if (!push())
                return false;
            emit(BC_DUP);
        }
        storeTarget(target, storedType);
        return true;
    }
};

bool buildBytecode(DeviceTypeCodeProgram& prog)
{
    DeviceTypeBytecodeBuilder builder(prog);
    return builder.build();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Run the bytecode
/// @note Compiled for speed with a jump table for the dispatch, also in a build optimised for size (where GCC makes
///       a switch a chain of compares)
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2", "jump-tables")))
#endif
DeviceTypeCodeProgram::Status DeviceTypeCodeProgram::runBytecode(RunContext& ctx) const
{
    Cell stack[BC_MAX_STACK];
    // The program's variables (at most BC_MAX_SLOTS)
    Cell* slots = (Cell*)alloca(sizeof(Cell) * (_slotTypes.size() + 1));
    memset(slots, 0, sizeof(Cell) * _slotTypes.size());
    Cell* sp = stack;
    const Ins* pCode = _code.data();
    const uint8_t* pBuf = ctx.pBuf;
    const int64_t bufLen = pBuf ? (int64_t)ctx.bufLen : 0;
    uint32_t pc = 0;
    // The budget is kept here, and stored back when the code ends
    uint32_t budget = ctx.budget;
    auto end = [&ctx, &budget](Status status) {
        ctx.budget = budget;
        return status;
    };
    auto bufAt = [pBuf, bufLen](int64_t index) -> int32_t {
        return ((index >= 0) && (index < bufLen)) ? pBuf[index] : 0;
    };
    while (true)
    {
        if (budget == 0)
            return end(Status::OutOfBudget);
        budget--;
        const Ins& ins = pCode[pc++];
        switch (ins.op)
        {
            // 32-bit int values
            case BC_PUSH_K: (sp++)->i32 = ins.b; break;
            case BC_LOAD_VAR: *sp++ = slots[ins.a]; break;
            case BC_STORE_VAR: slots[ins.a] = *--sp; break;
            case BC_LOAD_BUF: sp[-1].i32 = bufAt(sp[-1].i32); break;
            case BC_LOAD_BUF_VAR_K: (sp++)->i32 = bufAt(wrapAdd32(slots[ins.a].i32, ins.b)); break;
            case BC_LOAD_BUF_K: (sp++)->i32 = bufAt(ins.b); break;
            case BC_STORE_OUT:
            {
                const Cell& v = *--sp;
                if (ctx.pOut)
                    storeField(ctx.pOut + ins.b, (NumType)ins.t, cellToVal(v, (VT)ins.a));
                break;
            }
            case BC_NARROW: sp[-1].i32 = narrow32(sp[-1].i32, ins.t); break;
            case BC_DUP: *sp = sp[-1]; sp++; break;
            case BC_POP: sp--; break;
            case BC_ADD: sp--; sp[-1].i32 = wrapAdd32(sp[-1].i32, sp[0].i32); break;
            case BC_SUB: sp--; sp[-1].i32 = wrapSub32(sp[-1].i32, sp[0].i32); break;
            case BC_MUL: sp--; sp[-1].i32 = wrapMul32(sp[-1].i32, sp[0].i32); break;
            case BC_DIV: sp--; sp[-1].i32 = div32(sp[-1].i32, sp[0].i32); break;
            case BC_MOD: sp--; sp[-1].i32 = mod32(sp[-1].i32, sp[0].i32); break;
            case BC_SHL: sp--; sp[-1].i32 = shl32(sp[-1].i32, sp[0].i32); break;
            case BC_SHR: sp--; sp[-1].i32 = shr32(sp[-1].i32, sp[0].i32); break;
            case BC_AND: sp--; sp[-1].i32 &= sp[0].i32; break;
            case BC_OR: sp--; sp[-1].i32 |= sp[0].i32; break;
            case BC_XOR: sp--; sp[-1].i32 ^= sp[0].i32; break;
            case BC_LT: sp--; sp[-1].i32 = sp[-1].i32 < sp[0].i32; break;
            case BC_LE: sp--; sp[-1].i32 = sp[-1].i32 <= sp[0].i32; break;
            case BC_GT: sp--; sp[-1].i32 = sp[-1].i32 > sp[0].i32; break;
            case BC_GE: sp--; sp[-1].i32 = sp[-1].i32 >= sp[0].i32; break;
            case BC_EQ: sp--; sp[-1].i32 = sp[-1].i32 == sp[0].i32; break;
            case BC_NE: sp--; sp[-1].i32 = sp[-1].i32 != sp[0].i32; break;
            case BC_ADD_K: sp[-1].i32 = wrapAdd32(sp[-1].i32, ins.b); break;
            case BC_MUL_K: sp[-1].i32 = wrapMul32(sp[-1].i32, ins.b); break;
            case BC_AND_K: sp[-1].i32 &= ins.b; break;
            case BC_OR_K: sp[-1].i32 |= ins.b; break;
            case BC_XOR_K: sp[-1].i32 ^= ins.b; break;
            case BC_SHL_K: sp[-1].i32 = shl32(sp[-1].i32, ins.b); break;
            case BC_SHR_K: sp[-1].i32 = shr32(sp[-1].i32, ins.b); break;
            case BC_NEG: sp[-1].i32 = wrapSub32(0, sp[-1].i32); break;
            case BC_NOT: sp[-1].i32 = sp[-1].i32 == 0; break;
            case BC_BNOT: sp[-1].i32 = ~sp[-1].i32; break;
            case BC_BOOL: sp[-1].i32 = sp[-1].i32 != 0; break;
            case BC_ADD_VAR_K: slots[ins.a].i32 = narrow32(wrapAdd32(slots[ins.a].i32, ins.b), ins.t); break;
            case BC_JMP: pc = (uint32_t)ins.b; break;
            case BC_JZ: if ((--sp)->i32 == 0) pc = (uint32_t)ins.b; break;
            case BC_JNZ: if ((--sp)->i32 != 0) pc = (uint32_t)ins.b; break;
            case BC_JGE: sp -= 2; if (sp[0].i32 >= sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_JGT: sp -= 2; if (sp[0].i32 > sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_JLE: sp -= 2; if (sp[0].i32 <= sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_JLT: sp -= 2; if (sp[0].i32 < sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_JNE: sp -= 2; if (sp[0].i32 != sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_JEQ: sp -= 2; if (sp[0].i32 == sp[1].i32) pc = (uint32_t)ins.b; break;
            case BC_NEXT:
                if (!ctx.nextFn || !ctx.nextFn(ctx.pNextContext))
                {
                    if (ins.b < 0)
                        return end(Status::Break);
                    pc = (uint32_t)ins.b;
                }
                break;
            case BC_RET:
            {
                Val v = cellToVal(*--sp, (VT)ins.a);
                ctx.returnValue = (ins.t == 0) ? (int64_t)as<uint32_t>(v) : (as<bool>(v) ? 1 : 0);
                return end(Status::Return);
            }

            // Any type
            case BC_PUSH_C: *sp++ = _consts[ins.b]; break;
            case BC_CONV: sp[-1] = valToCell(convertTo(cellToVal(sp[-1], (VT)ins.a), (VT)ins.t)); break;
            case BC_NARROW_V: sp[-1] = valToCell(narrowTo(cellToVal(sp[-1], (VT)ins.a), (NumType)ins.t)); break;
            case BC_BIN:
            {
                VT t = (VT)ins.t;
                bool isShift = (ins.a == OP_SHL) || (ins.a == OP_SHR);
                Val lhs = cellToVal(sp[-2], t);
                Val rhs = cellToVal(sp[-1], isShift ? VT::I64 : t);
                sp--;
                sp[-1] = valToCell(binary((uint8_t)ins.a, t, lhs, rhs));
                break;
            }
            case BC_UN: sp[-1] = valToCell(unary((uint8_t)ins.a, (VT)ins.t, cellToVal(sp[-1], (VT)ins.t))); break;
            case BC_TRUTHY: sp[-1].i32 = truthy(cellToVal(sp[-1], (VT)ins.t)) ? 1 : 0; break;
            case BC_LOAD_BUF_V:
            {
                Val indexVal = cellToVal(sp[-1], (VT)ins.t);
                int64_t index = as<int64_t>(indexVal);
                if ((indexVal.t == VT::U64) && (indexVal.u > (uint64_t)INT64_MAX))
                    index = -1;
                sp[-1].i32 = bufAt(index);
                break;
            }
            case BC_LOAD_OUT:
            {
                Cell c;
                c.u64 = 0;
                if (ctx.pOut)
                    c = valToCell(loadField(ctx.pOut + ins.b, (NumType)ins.t));
                *sp++ = c;
                break;
            }
            case BC_LOAD_INIT:
                (sp++)->i32 = ((uint32_t)ins.a < ctx.numInitValues) ? ctx.pInitValues[ins.a] : 0;
                break;
            case BC_STORE_INIT:
            {
                int32_t v = (--sp)->i32;
                if ((uint32_t)ins.a < ctx.numInitValues)
                    ctx.pInitValues[ins.a] = v;
                break;
            }
            case BC_ARR_CHECK:
                if ((sp[-1].i32 < 0) || (sp[-1].i32 >= ins.a))
                {
                    sp--;
                    pc = (uint32_t)ins.b;
                }
                break;
            case BC_ARR_STORE:
            {
                const Cell& v = *--sp;
                int32_t index = (--sp)->i32;
                if (ctx.pOut)
                    storeField(ctx.pOut + ins.b + (uint32_t)index * storeSize((NumType)ins.t), (NumType)ins.t,
                                cellToVal(v, (VT)ins.a));
                break;
            }
            default:
                return end(Status::Done);
        }
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
