#!/usr/bin/env python3
"""Tests for array outputs in custom decode pseudocode (out.<name>[i] = value;)

Run from RaftCore/scripts:  python3 -m unittest tests.test_pseudocode_arrays -v
The compile-and-run tests need g++ on the PATH (skipped otherwise).
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from DecodeGenerator import DecodeGenerator          # noqa: E402
from PseudocodeHandler import PseudocodeHandler      # noqa: E402

GEN_OPTIONS = {"POLL_RESULT_TIMESTAMP_SIZE": 2, "POLL_RESULT_RESOLUTION_US": 100}


def make_record(attrs, code, b, us=None):
    resp = {"b": b, "a": attrs, "c": {"n": "test_fn", "c": code}}
    if us is not None:
        resp["us"] = us
    return {"devInfoJson": {"resp": resp}}


# Single sample (no next): scalar + int16 array, one write out of range
REC_SINGLE = make_record(
    [{"n": "seq", "t": "B", "o": "uint8"},
     {"n": "vals", "t": "<h[4]", "o": "int16"}],
    "out.seq=buf[0];int i=0;while(i<4){out.vals[i]=(buf[2+i*2]<<8)|buf[1+i*2];i++;}out.vals[7]=99;out.vals[0-1]=98;",
    9)

# Several samples per poll result via next; each sample writes a variable number of elements
# (unwritten elements must be zero, not left over from the previous sample)
REC_NEXT = make_record(
    [{"n": "a", "t": "B", "o": "uint8"},
     {"n": "g", "t": "B[3]", "o": "uint8"}],
    "int n=buf[0];int k=1;int s=0;while(s<n){out.a=s;int j=0;while(j<buf[k]){out.g[j]=buf[k+1+j];j++;}k+=4;s++;next;}",
    9, us=1000)

# Signed element type stored as float - raw 0xFFFF must become -1
REC_SIGNED_FLOAT = make_record(
    [{"n": "f", "t": "<h[2]", "o": "float"}],
    "out.f[0]=(buf[1]<<8)|buf[0];out.f[1]=(buf[3]<<8)|buf[2];next;",
    4)

# Existing-style scalar decode (from MAX30101) - must generate exactly as before
REC_SCALAR = make_record(
    [{"n": "Red", "t": ">I", "o": "uint32"}, {"n": "IR", "t": ">I", "o": "uint32"}],
    "int N=(buf[0]+32-buf[2])%32;int k=3;int i=0;while(i<N){out.Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];"
    "out.IR=(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];k+=6;i++;next;}",
    51, us=40000)


class TestGeneration(unittest.TestCase):

    def gen(self, rec, key="T"):
        return DecodeGenerator(GEN_OPTIONS).decode_fn(rec, key)

    def test_array_write_is_bounds_checked(self):
        code = self.gen(REC_SINGLE)
        self.assertIn("if ((__ai >= 0) && (__ai < 4)) pOut->vals[__ai] = ((buf[2+i*2]<<8)|buf[1+i*2]);", code)
        self.assertIn("for (int __z = 0; __z < 4; __z++) pOut->vals[__z] = 0;", code)

    def test_indexed_write_to_scalar_fails(self):
        rec = make_record([{"n": "x", "t": "B", "o": "uint8"}], "out.x[0]=1;", 1)
        with self.assertRaises(ValueError):
            self.gen(rec)

    def test_compound_assignment_fails(self):
        rec = make_record([{"n": "g", "t": "B[3]", "o": "uint8"}], "out.g[0]+=1;", 3)
        with self.assertRaises(ValueError):
            self.gen(rec)

    def test_scalar_write_to_array_warns(self):
        rec = make_record([{"n": "g", "t": "B[3]", "o": "uint8"}], "out.g=1;", 3)
        out = io.StringIO()
        with redirect_stdout(out):
            self.gen(rec)
        self.assertIn("WARNING", out.getvalue())

    def test_scalar_only_decode_unchanged(self):
        code = self.gen(REC_SCALAR, "MAX30101")
        self.assertNotIn("__ai", code)
        self.assertNotIn("__z", code)
        self.assertIn("pOut->Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];", code)

    def test_array_sign_extension_per_element(self):
        code = self.gen(REC_SIGNED_FLOAT)
        self.assertIn("for (int __s = 0; __s < 2; __s++) if (pOut->f[__s] >= 32768.0f) pOut->f[__s] -= 65536.0f;", code)

    def test_field_desc_count(self):
        desc = DecodeGenerator(GEN_OPTIONS).get_field_desc_def(REC_SINGLE["devInfoJson"], "T")
        self.assertIn('{"seq", (uint16_t)offsetof(poll_T, seq), AttrType::Uint8, "", 1.0f, 0.0f, 1}', desc)
        self.assertIn('{"vals", (uint16_t)offsetof(poll_T, vals), AttrType::Int16, "", 1.0f, 0.0f, 4}', desc)

    def test_nested_brackets_in_index_and_value(self):
        handler = PseudocodeHandler()
        tokens = list(handler.lexer("out.g[buf[0]]=buf[buf[1]];"))
        seen = []
        handler.rewrite_array_writes(tokens, {"g": 3},
            lambda n, c, i, v: seen.append((handler.tokens_to_expr(i), handler.tokens_to_expr(v))) or "X")
        self.assertEqual(seen, [("buf[0]", "buf[buf[1]]")])


HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <cstddef>
#include <vector>
namespace Raft {
    uint16_t getBEUInt16AndInc(const uint8_t*& p, const uint8_t* pEnd = nullptr) { uint16_t v = (p[0] << 8) | p[1]; p += 2; return v; }
}
using namespace Raft;
struct DevicePollingInfo { static const uint32_t POLL_RESULT_RESOLUTION_US = 100; static const uint32_t POLL_RESULT_WRAP_VALUE = 65536; };
struct RaftBusDeviceDecodeState { uint64_t lastReportTimestampUs = 0; uint64_t reportTimestampOffsetUs = 0; };

%STRUCTS%

static auto decode_single = %FN_SINGLE%;
static auto decode_next = %FN_NEXT%;
static auto decode_signed = %FN_SIGNED%;

int main() {
    RaftBusDeviceDecodeState st;
    // Single: ts, seq=7, vals = 1, -2, 300, -32768
    std::vector<uint8_t> b1 = {0, 10, 7, 0x01,0x00, 0xFE,0xFF, 0x2C,0x01, 0x00,0x80};
    poll_SINGLE s1[2] = {};
    s1[0].vals[0] = 555;   // must be overwritten / cleared
    decode_single(b1.data(), b1.size(), s1, sizeof(s1), 2, st);
    printf("single %d %d %d %d %d\n", s1[0].seq, s1[0].vals[0], s1[0].vals[1], s1[0].vals[2], s1[0].vals[3]);

    // Next: 2 samples; sample 0 writes 3 elements, sample 1 writes 1 element
    std::vector<uint8_t> b2 = {0, 10, 2, 3,11,12,13, 1,21,0,0};
    poll_NEXT s2[4];
    for (auto& r : s2) { r.a = 0xEE; r.g[0] = r.g[1] = r.g[2] = 0xEE; }
    uint32_t n2 = decode_next(b2.data(), b2.size(), s2, sizeof(s2), 4, st);
    printf("next %u | %d %d %d %d | %d %d %d %d\n", n2, s2[0].a, s2[0].g[0], s2[0].g[1], s2[0].g[2],
           s2[1].a, s2[1].g[0], s2[1].g[1], s2[1].g[2]);

    // Signed float: 0xFFFF -> -1, 0x0002 -> 2
    std::vector<uint8_t> b3 = {0, 10, 0xFF,0xFF, 0x02,0x00};
    poll_SIGNED s3[2] = {};
    uint32_t n3 = decode_signed(b3.data(), b3.size(), s3, sizeof(s3), 2, st);
    printf("signed %u %.1f %.1f\n", n3, s3[0].f[0], s3[0].f[1]);
    return 0;
}
"""


@unittest.skipUnless(shutil.which("g++"), "g++ not available")
class TestCompiledDecode(unittest.TestCase):

    def test_compile_and_run(self):
        gen = DecodeGenerator(GEN_OPTIONS)
        recs = {"SINGLE": REC_SINGLE, "NEXT": REC_NEXT, "SIGNED": REC_SIGNED_FLOAT}
        src = HARNESS.replace("%STRUCTS%", "\n".join(gen.get_struct_defs(recs)))
        src = src.replace("%FN_SINGLE%", gen.decode_fn(REC_SINGLE, "SINGLE"))
        src = src.replace("%FN_NEXT%", gen.decode_fn(REC_NEXT, "NEXT"))
        src = src.replace("%FN_SIGNED%", gen.decode_fn(REC_SIGNED_FLOAT, "SIGNED"))
        with tempfile.TemporaryDirectory() as tmp:
            cpp = os.path.join(tmp, "t.cpp")
            exe = os.path.join(tmp, "t")
            with open(cpp, "w") as f:
                f.write(src)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Werror", "-Wno-unused-variable",
                            "-Wno-unused-but-set-variable", "-o", exe, cpp], check=True)
            out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()
        self.assertEqual(out[0], "single 7 1 -2 300 -32768")
        # Unwritten elements of the second sample are zero, not carried over from the first
        self.assertEqual(out[1], "next 2 | 0 11 12 13 | 1 21 0 0")
        self.assertEqual(out[2], "signed 1 -1.0 2.0")


if __name__ == "__main__":
    unittest.main()
