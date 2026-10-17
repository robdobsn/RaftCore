"""Compile generated standard decoders and check packed values against Raft's browser conventions."""

import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from DecodeGenerator import DecodeGenerator
import AssembleDeviceTypes


@unittest.skipUnless(shutil.which("g++"), "g++ required for generated-decoder regression")
class StandardDecodeTests(unittest.TestCase):
    def test_packed_aht20_and_both_shift_directions(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        catalogue, _ = AssembleDeviceTypes.load_catalogue(str(root / "devtypes" / "records"))
        aht = {"devTypes": catalogue}
        records = {"AHT20": aht["devTypes"]["AHT20"]}
        records["SHIFT"] = {"devInfoJson": {"resp": {"b": 8, "a": [
            {"n": "right", "t": ">I", "o": "uint32", "s": 1},
            {"n": "left", "t": ">I", "o": "int32", "s": -1}
        ]}}}
        records["VL6180"] = aht["devTypes"]["VL6180"]
        records["LUT"] = {"devInfoJson": {"resp": {"b": 3, "a": [
            {"n": "code", "t": "B", "o": "uint8", "lut": [{"r": "1,2", "v": 10}, {"r": "0x20-0x30", "v": 20},
                                                         {"r": "", "v": 99}]},
            {"n": "keep", "t": "B", "o": "uint8", "lut": [{"r": "5", "v": 50}]},
            {"n": "scaled", "t": "B", "o": "float", "d": 4, "lut": [{"r": "3", "v": -1}]}
        ]}}}
        generator = DecodeGenerator({"POLL_RESULT_RESOLUTION_US": 100})
        source = r"""
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cmath>
struct RaftBusDeviceDecodeState { uint64_t lastReportTimestampUs=0, reportTimestampOffsetUs=0; };
struct DevicePollingInfo {
    static constexpr uint64_t POLL_RESULT_WRAP_VALUE=65536, POLL_RESULT_RESOLUTION_US=100;
};
uint8_t getUInt8AndInc(const uint8_t*& p, const uint8_t* end) { assert(p<end); return *p++; }
uint16_t getBEUInt16AndInc(const uint8_t*& p, const uint8_t* end) {
    uint16_t value=getUInt8AndInc(p,end); return (value<<8)|getUInt8AndInc(p,end);
}
uint32_t getBEUInt32AndInc(const uint8_t*& p, const uint8_t* end) {
    uint32_t value=0;
    for(int i=0;i<4;++i) value=(value<<8)|getUInt8AndInc(p,end);
    return value;
}
"""
        source += "\n".join(generator.get_struct_defs(records))
        source += "\nauto aht = " + generator.decode_fn(records["AHT20"], "AHT20") + ";\n"
        source += "auto shift = " + generator.decode_fn(records["SHIFT"], "SHIFT") + ";\n"
        source += "auto vl6180 = " + generator.decode_fn(records["VL6180"], "VL6180") + ";\n"
        source += "auto lut = " + generator.decode_fn(records["LUT"], "LUT") + ";\n"
        source += r"""
int main() {
    uint8_t humidity[]={0,10,8,128,0,6,0,0};
    poll_AHT20 a{};
    RaftBusDeviceDecodeState state;
    assert(aht(humidity,sizeof humidity,&a,sizeof a,1,state)==1);
    assert(std::fabs(a.humidity-50)<0.001);
    assert(std::fabs(a.temperature-25)<0.001);
    uint8_t values[]={0,10,0x80,0,0,0,0x40,0,0,0};
    poll_SHIFT result{};
    assert(shift(values,sizeof values,&result,sizeof result,1,state)==1);
    assert(result.right==0x40000000);
    assert(result.left==INT32_MIN);

    // Lookup tables: VL6180 "valid" is 1 for range status (bits 7-4) 0, else 0
    uint8_t rangeOk[]={0,10, 0x00,0,0x04,42, 0};
    uint8_t rangeErr[]={0,10, 0xB0,0,0x04,42, 0};
    poll_VL6180 v{};
    assert(vl6180(rangeOk,sizeof rangeOk,&v,sizeof v,1,state)==1);
    assert(v.valid && v.err==0 && v.dist==42);
    assert(vl6180(rangeErr,sizeof rangeErr,&v,sizeof v,1,state)==1);
    assert(!v.valid && v.err==11);

    // Lookup tables: list, range, default, no default (kept), and after the divisor (12/4 = 3)
    poll_LUT l{};
    uint8_t lutA[]={0,10, 2,5,12};
    assert(lut(lutA,sizeof lutA,&l,sizeof l,1,state)==1);
    assert(l.code==10 && l.keep==50 && l.scaled==-1);
    uint8_t lutB[]={0,10, 0x25,6,14};
    assert(lut(lutB,sizeof lutB,&l,sizeof l,1,state)==1);
    assert(l.code==20 && l.keep==6 && std::fabs(l.scaled-3.5)<0.001);
    uint8_t lutC[]={0,10, 7,6,0};
    assert(lut(lutC,sizeof lutC,&l,sizeof l,1,state)==1);
    assert(l.code==99);
}
"""
        with tempfile.TemporaryDirectory() as folder:
            binary = str(pathlib.Path(folder) / "regression")
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Werror", "-Wno-unused-variable",
                            "-Wno-unused-but-set-variable", "-fsanitize=undefined", "-x", "c++", "-",
                            "-o", binary], input=source, text=True, check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
