/* profiler.h - minimal function-level profiler (v0.5 roadmap: performance
 * profiling integration; flamegraph export is a follow-up). */
#ifndef INIMERSE_PROFILER_H
#define INIMERSE_PROFILER_H

struct VM;

/* Start recording function calls (inclusive wall time + call counts). */
void prof_enable(struct VM *vm);

/* Feed the profiler from the interpreter: call after the frame is pushed
 * (depth = frame_count after increment), and on return before the frame is
 * popped (depth = frame_count before decrement). Both are no-ops when
 * profiling is disabled; prof_record_return pops unbalanced frames so
 * exception unwinds stay consistent. */
void prof_record_call(struct VM *vm, int depth, const char *func_name);
void prof_record_return(struct VM *vm, int depth);

/* Stop recording, write a text report to out_path (one line per function:
 * calls total_ms max_ms name, sorted by total time) and print the top
 * functions to stdout. Safe to call when profiling was never enabled. */
void prof_finish(struct VM *vm, const char *out_path);

#endif /* INIMERSE_PROFILER_H */
