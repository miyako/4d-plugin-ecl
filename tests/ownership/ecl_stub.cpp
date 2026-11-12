/*
 * Synthetic ECL boundary for the ownership harness.
 *
 * Provides exactly the ECL entry points 4DPlugin-ECL.cpp links against, so the
 * real plugin source can run without the bundled libecl. Nothing is read or
 * evaluated: si_string_to_object only records the UTF-32 source it was given,
 * si_safe_eval returns a configured outcome, and the *STANDARD-OUTPUT* /
 * *ERROR-OUTPUT* string streams return configured text.
 */
#include <ecl/ecl.h>

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ecl_stub.h"

cl_symbol_initializer cl_symbols[2048];

namespace {

std::u32string g_out, g_err, g_source;
bool g_eval_ok = true;
int g_eval_calls = 0;
std::vector<cl_object> g_allocated;
std::map<cl_object, std::string> g_symbol_names;
std::map<cl_object, std::string> g_stream_roles;

cl_object alloc_object() {
    cl_object o = (cl_object)calloc(1, sizeof(union cl_lispunion));
    g_allocated.push_back(o);
    return o;
}

cl_object make_string(const std::u32string &s) {
    cl_object o = ecl_alloc_simple_vector(s.size(), ecl_aet_ch);
    for(size_t i = 0; i < s.size(); i++) o->string.self[i] = (ecl_character)s[i];
    return o;
}

} // namespace

void ecl_stub_configure(const std::u32string &out, const std::u32string &err, bool eval_ok) {
    g_out = out;
    g_err = err;
    g_eval_ok = eval_ok;
    g_eval_calls = 0;
    g_source.clear();
}

const std::u32string &ecl_stub_last_source() { return g_source; }

int ecl_stub_eval_calls() { return g_eval_calls; }

void ecl_stub_reset() {
    for(cl_object o : g_allocated) {
        if(o->d.t == t_string) free(o->string.self);
        free(o);
    }
    g_allocated.clear();
    g_symbol_names.clear();
    g_stream_roles.clear();
}

int cl_boot(int, char **) { return 1; }
bool ecl_import_current_thread(cl_object, cl_object) { return true; }
void ecl_release_current_thread(void) {}
cl_fixnum ecl_get_option(int) { return 0; }
void ecl_set_option(int, cl_fixnum) {}
void ecl_tcp_close_all(void) {}
cl_env_ptr ecl_process_env(void) { return 0; }
cl_object ecl_symbol_value(cl_object) { return ECL_NIL; }
cl_object ecl_elt_set(cl_object, cl_fixnum, cl_object val) { return val; }
cl_object cl_list(cl_narg, ...) { return alloc_object(); }

cl_object ecl_alloc_simple_vector(cl_index l, cl_elttype aet) {
    if(aet != ecl_aet_ch) abort();
    cl_object o = alloc_object();
    o->string.t = t_string;
    o->string.elttype = ecl_aet_ch;
    o->string.dim = l;
    o->string.fillp = l;
    o->string.self = (ecl_character *)calloc(l + 1, sizeof(ecl_character));
    return o;
}

cl_object ecl_make_symbol(const char *s, const char *) {
    cl_object o = alloc_object();
    g_symbol_names[o] = s;
    return o;
}

cl_object ecl_make_string_output_stream(cl_index, int) { return alloc_object(); }

cl_object ecl_setq(cl_env_ptr, cl_object var, cl_object value) {
    g_stream_roles[value] = g_symbol_names[var];
    return value;
}

cl_object cl_get_output_stream_string(cl_object strm) {
    const std::string &role = g_stream_roles[strm];
    if(role == "*STANDARD-OUTPUT*") return make_string(g_out);
    if(role == "*ERROR-OUTPUT*") return make_string(g_err);
    abort();
}

cl_object si_string_to_object(cl_narg, cl_object str, ...) {
    g_source.assign((const char32_t *)str->string.self, str->string.dim);
    return alloc_object();
}

cl_object si_safe_eval(cl_narg narg, cl_object, cl_object env, ...) {
    g_eval_calls++;
    cl_object sentinel = OBJNULL;
    if(narg >= 3) {
        va_list ap;
        va_start(ap, env);
        sentinel = va_arg(ap, cl_object);
        va_end(ap);
    }
    return g_eval_ok ? alloc_object() : sentinel;
}
