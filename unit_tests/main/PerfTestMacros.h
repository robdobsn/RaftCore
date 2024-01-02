
#pragma once

#define EVAL_PERF_START() perfStartTimeUs = micros();
#define EVAL_PERF_CLEAR(SVar) SVar = 0
#define EVAL_PERF_CLEAR_START(SVar) { SVar = 0; perfStartTimeUs = micros(); }
#define EVAL_PERF_ACCUM(SVar) {SVar += (micros() - perfStartTimeUs);}
#define EVAL_PERF_END(SVar) {SVar = (micros() - perfStartTimeUs);}
