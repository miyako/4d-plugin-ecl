/*
 * Control surface of the synthetic ECL boundary (ecl_stub.cpp). Kept free of
 * ECL headers so the engine harness can be compiled as plain C++17.
 */
#ifndef ECL_STUB_H
#define ECL_STUB_H

#include <string>

/* Configures what the next `lisp` call "prints" and whether evaluation
   succeeds. No Lisp is ever read or evaluated. */
void ecl_stub_configure(const std::u32string &out, const std::u32string &err, bool eval_ok);

/* UTF-32 source the plugin handed to si_string_to_object on the last call. */
const std::u32string &ecl_stub_last_source();

/* Number of si_safe_eval calls since the last configure. */
int ecl_stub_eval_calls();

/* Frees every stub-allocated ECL object (ECL itself is garbage collected). */
void ecl_stub_reset();

#endif
