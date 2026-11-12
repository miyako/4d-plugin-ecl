/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Links the real plugin sources (4DPlugin-ECL.cpp, support/4DPlugin-JSON.cpp)
 * and the bundled, unmodified 4D Plugin API (4DPluginAPI.c). Only gCall4D is
 * replaced: it models engine-side allocations (Unistrings, pictures, objects,
 * collections, handles) with malloc/free so ASan reports double frees and
 * use-after-free, and it keeps a live set of every engine resource so leaks
 * are detected exactly. ECL is replaced by ecl_stub.cpp; no Lisp is evaluated.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_CreateVariable(eVK_Unistring/eVK_Picture) allocates an empty value.
 *  - PA_SetObjectProperty deep-copies strings/pictures and retains
 *    objects/collections; the caller still owns its variable.
 *  - PA_GetObjectProperty returns an owned copy (strings/pictures copied,
 *    objects/collections retained) that must be cleared; on error the result
 *    is not initialised (poisoned here).
 *  - PA_ExecuteCommandByID returns an owned result; on error it is likewise
 *    poisoned here.
 *  - PA_ClearVariable releases what the variable owns.
 */

#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include "4DPlugin-JSON.h"
#include "ecl_stub.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>


namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    bool collection;
    std::map<u16, PA_Variable> props;
};

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
bool g_fail_get = false;
bool g_fail_command = false;
int g_poison_touched = 0;
/* Stands in for the uninitialised result of a failed engine call: readable
   (so the test fails by assertion, not by crashing) but never engine-owned. */
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "ENGINE VIOLATION: %s\n", msg);
    fflush(stderr);
    abort();
}

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) { g_poison_touched++; s->fString = 0; return; }
    if(!g_strings.erase(s->fString)) die("disposed a string the engine does not own");
    free(s->fString);
    s->fString = 0;
    s->fLength = 0;
}

void *new_picture() {
    void *p = malloc(16);
    g_pictures.insert(p);
    return p;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) die("disposed a picture the engine does not own");
    free(p);
}

Obj *new_object(bool collection = false) {
    Obj *o = new Obj();
    o->rc = 1;
    o->collection = collection;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        default: break;
    }
    v->fType = eVK_Undefined;
}

void retain(Obj *o) {
    if(!o) return;
    if(!g_objects.count(o)) die("retained a dead object");
    o->rc++;
}

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Picture:
            dst.uValue.fPicture = new_picture();
            break;
        case eVK_Object: retain((Obj *)src.uValue.fObject); break;
        case eVK_Collection: retain((Obj *)src.uValue.fCollection); break;
        default: break;
    }
    return dst;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) die("released a dead object");
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

PA_Variable poisoned() {
    PA_Variable v;
    memset(&v, 0, sizeof(v));
    v.fType = eVK_Unistring;
    v.uValue.fString.fString = kPoison;
    v.uValue.fString.fLength = 4;
    return v;
}

/* UTF-8 <-> UTF-32 (host endian), as PA_ConvertCharsetToCharset. Returns the
   number of bytes written, or 0 if the output does not fit. */
size_t convert(const char *from, size_t fromSize, int fromCs, char *to, size_t toSize, int toCs) {
    std::u32string cps;
    if(fromCs == eVTC_UTF_8 && toCs == eVTC_UTF_32) {
        const unsigned char *p = (const unsigned char *)from, *e = p + fromSize;
        while(p < e) {
            char32_t c = *p++;
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if(extra) c &= (0x3F >> extra);
            while(extra-- && p < e) c = (c << 6) | (*p++ & 0x3F);
            cps.push_back(c);
        }
        size_t n = cps.size() * sizeof(char32_t);
        if(n > toSize) return 0;
        memcpy(to, cps.data(), n);
        return n;
    }
    if(fromCs == eVTC_UTF_32 && toCs == eVTC_UTF_8) {
        std::string out;
        const char32_t *p = (const char32_t *)from;
        for(size_t i = 0; i < fromSize / sizeof(char32_t); i++) {
            char32_t c = p[i];
            if(c < 0x80) out.push_back((char)c);
            else if(c < 0x800) { out.push_back((char)(0xC0 | (c >> 6))); out.push_back((char)(0x80 | (c & 0x3F))); }
            else if(c < 0x10000) { out.push_back((char)(0xE0 | (c >> 12))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
            else { out.push_back((char)(0xF0 | (c >> 18))); out.push_back((char)(0x80 | ((c >> 12) & 0x3F))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
        }
        if(out.size() > toSize) return 0;
        memcpy(to, out.data(), out.size());
        return out.size();
    }
    die("unmodelled charset conversion");
}

struct Handle { char *data; };

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_GET_INFORMATION:
            eb->fParam2 = 0x00002000; /* v20: PA_CreateObject uses New object */
            break;
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_CREATE_PICTURE:
            eb->fPicture = new_picture();
            break;
        case EX_DISPOSE_PICTURE:
            free_picture(eb->fPicture);
            break;
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_CONVERT_CHARSET_TO_CHARSET:
            eb->fParam3 = (sLONG_PTR)convert((const char *)eb->fPtr1, (size_t)eb->fParam1, (int)eb->fParam2,
                                             (char *)eb->fPtr2, (size_t)eb->fParam3, (int)eb->fParam4);
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = (Obj *)eb->fPtr1;
            if(!g_objects.count(o)) die("set on a dead object");
            u16 k = key_of(eb->fUniString1);
            auto it = o->props.find(k);
            if(it != o->props.end()) clear_var(&it->second);
            o->props[k] = copy_var(*(PA_Variable *)eb->fPtr2);
            break;
        }
        case EX_GET_OBJ_VALUE: {
            Obj *o = (Obj *)eb->fPtr1;
            PA_Variable *out = (PA_Variable *)eb->fPtr2;
            if(g_fail_get) { *out = poisoned(); eb->fError = -1; break; }
            if(!g_objects.count(o)) die("get on a dead object");
            auto it = o->props.find(key_of(eb->fUniString1));
            if(it == o->props.end()) {
                memset(out, 0, sizeof(*out));
                out->fType = eVK_Undefined;
            } else {
                *out = copy_var(it->second);
            }
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            PA_Variable **params = (PA_Variable **)eb->fPtr1;
            PA_Variable *ret = (PA_Variable *)eb->fPtr2;
            switch(eb->fParam1) {
                case 1471: /* New object */
                    ret->fType = eVK_Object;
                    ret->uValue.fObject = new_object();
                    break;
                case 1231: { /* OB Is defined */
                    Obj *o = (Obj *)params[0]->uValue.fObject;
                    ret->fType = eVK_Boolean;
                    ret->uValue.fBoolean = o && o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
                    break;
                }
                case 1225: { /* OB Copy */
                    Obj *src = (Obj *)params[0]->uValue.fObject;
                    if(!g_objects.count(src)) die("copied a dead object");
                    Obj *dst = new_object();
                    for(auto &kv : src->props) dst->props[kv.first] = copy_var(kv.second);
                    ret->fType = eVK_Object;
                    ret->uValue.fObject = dst;
                    break;
                }
                case 1217: { /* JSON Stringify */
                    if(g_fail_command) { *ret = poisoned(); eb->fError = -1; break; }
                    Obj *o = (Obj *)params[0]->uValue.fObject;
                    if(!g_objects.count(o)) die("stringified a dead object");
                    char buf[32];
                    snprintf(buf, sizeof(buf), "{%zu}", o->props.size());
                    u16 s;
                    for(char *p = buf; *p; p++) s.push_back((PA_Unichar)*p);
                    ret->fType = eVK_Unistring;
                    ret->uValue.fString = new_string(s.c_str(), s.size());
                    break;
                }
                default:
                    die("unexpected PA_ExecuteCommandByID");
            }
            break;
        }
        case EX_HANDLE_MANAGER: {
            switch(eb->fParam1) {
                case 1: {
                    Handle *h = (Handle *)malloc(sizeof(Handle));
                    h->data = (char *)malloc(eb->fParam2 ? eb->fParam2 : 1);
                    g_handles.insert(h);
                    eb->fHandle = (PA_Handle)h;
                    break;
                }
                case 2: {
                    Handle *h = (Handle *)eb->fHandle;
                    if(!g_handles.erase(h)) die("disposed an unknown handle");
                    free(h->data);
                    free(h);
                    break;
                }
                case 5: eb->fParam3 = (sLONG_PTR)((Handle *)eb->fHandle)->data; break;
                case 6: break;
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

/* ---------- test helpers ---------- */

u16 W(const char *ascii) {
    u16 s;
    for(const char *p = ascii; *p; p++) s.push_back((PA_Unichar)(unsigned char)*p);
    return s;
}

PA_Unistring U(const u16 &s) { return new_string(s.c_str(), s.size()); }
PA_Unistring U(const char *ascii) { return U(W(ascii)); }

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

PA_Variable zero_var() { PA_Variable v; memset(&v, 0, sizeof(v)); return v; }
PA_Variable real_var(double d) { PA_Variable v = zero_var(); PA_SetRealVariable(&v, d); return v; }
PA_Variable bool_var(bool b) { PA_Variable v = zero_var(); PA_SetBooleanVariable(&v, b); return v; }
PA_Variable date_var(short d, short m, short y) { PA_Variable v = zero_var(); PA_SetDateVariable(&v, d, m, y); return v; }

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = zero_var();
    PA_Unistring u = U(value);
    PA_SetStringVariable(&v, &u);
    put(o, key, v);
    PA_ClearVariable(&v);
}

/* Stores child in o and drops the creator's reference: o holds the only one. */
void put_ref(Obj *o, const char *key, Obj *child) {
    PA_Variable v = zero_var();
    if(child->collection) PA_SetCollectionVariable(&v, (PA_CollectionRef)child);
    else PA_SetObjectVariable(&v, (PA_ObjectRef)child);
    put(o, key, v);
    PA_ClearVariable(&v);
}

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool no_leaks() {
    bool ok = true;
    if(!g_strings.empty()) { fprintf(stderr, "  LEAK: %zu engine string(s)\n", g_strings.size()); ok = false; }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    if(g_poison_touched) { fprintf(stderr, "  cleared %d failed-call result(s)\n", g_poison_touched); ok = false; }
    return ok;
}

const PA_Variable *prop(Obj *o, const char *key) {
    auto it = o->props.find(W(key));
    return it == o->props.end() ? 0 : &it->second;
}

u16 text_prop(Obj *o, const char *key) {
    const PA_Variable *v = prop(o, key);
    if(!v || v->fType != eVK_Unistring) return W("<not text>");
    return u16(v->uValue.fString.fString, v->uValue.fString.fLength);
}

int bool_prop(Obj *o, const char *key) {
    const PA_Variable *v = prop(o, key);
    if(!v || v->fType != eVK_Boolean) return -1;
    return v->uValue.fBoolean ? 1 : 0;
}

/* Runs the `lisp` command (selector 1) through the real PluginMain entry point.
   A null source omits parameter 1. Returns the object the command returned. */
Obj *run_lisp(const u16 *source) {
    PA_Unistring p1 = source ? U(*source) : PA_Unistring();
    void *params[2] = { source ? &p1 : 0, 0 };
    PA_ObjectRef result = 0;
    PluginBlock block;
    block.fParameters = params;
    block.fResult = &result;
    block.fData = 0;
    PluginMain(1, &block);
    if(source) PA_DisposeUnistring(&p1);
    ecl_stub_reset();
    Obj *o = (Obj *)result;
    if(!o || !g_objects.count(o)) die("lisp did not return a live object");
    return o;
}

/* "(print "é😀")" as UTF-16 (surrogate pair) and the matching UTF-32. */
const u16 kSourceU16 = { '(', 'p', 'r', 'i', 'n', 't', ' ', '"', 0x00E9, 0xD83D, 0xDE00, '"', ')' };
const std::u32string kSourceU32 = U"(print \"\u00E9\U0001F600\")";
const std::u32string kOutU32 = U"\n\"\u00E9\U0001F600\" ";
const u16 kOutU16 = { '\n', '"', 0x00E9, 0xD83D, 0xDE00, '"', ' ' };

/* ---------- tests ---------- */

typedef std::function<bool()> Test;

std::map<std::string, Test> tests() {
    std::map<std::string, Test> t;

    /* Live defects: every `lisp` call stores stdout/stderr (and, on early
       errors, stderr) through ob_set_s(wchar_t key), which leaks the empty
       string allocated by PA_CreateVariable(eVK_Unistring). */
    t["defect_cmd_lisp_success"] = [] {
        ecl_stub_configure(kOutU32, U"", true);
        Obj *r = run_lisp(&kSourceU16);
        bool ok = check(bool_prop(r, "success") == 1, "success true");
        ok = check(text_prop(r, "stdout") == kOutU16, "stdout round-trips UTF-32 -> UTF-16") && ok;
        ok = check(text_prop(r, "stderr").empty(), "stderr empty") && ok;
        release_object(r);
        return no_leaks() && ok;
    };
    t["defect_cmd_lisp_eval_failure"] = [] {
        ecl_stub_configure(U"", U"error", false);
        u16 src = W("(error)");
        Obj *r = run_lisp(&src);
        bool ok = check(bool_prop(r, "success") == 0, "success false");
        ok = check(text_prop(r, "stderr") == W("error"), "stderr captured") && ok;
        release_object(r);
        return no_leaks() && ok;
    };
    t["defect_cmd_lisp_missing_parameter"] = [] {
        ecl_stub_configure(U"", U"", true);
        Obj *r = run_lisp(0);
        bool ok = check(bool_prop(r, "success") == 0, "success false");
        ok = check(text_prop(r, "stderr") == W("missing lisp expression (parameter 1)"), "stderr message") && ok;
        ok = check(ecl_stub_eval_calls() == 0, "nothing evaluated") && ok;
        release_object(r);
        return no_leaks() && ok;
    };

    /* Dormant defects (no caller in this plugin): the other text/picture
       setters leak the same empty owner. */
    t["defect_ob_set_s_wkey_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_s((PA_ObjectRef)o, L"k", "value");
        bool ok = check(text_prop(o, "k") == W("value"), "value");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_set_s_ckey_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_s((PA_ObjectRef)o, "k", "value");
        bool ok = check(text_prop(o, "k") == W("value"), "value");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_set_a_wchar_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_a((PA_ObjectRef)o, L"k", L"value");
        bool ok = check(text_prop(o, "k") == W("value"), "value");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_set_a_u16_empty_owner"] = [] {
        Obj *o = new_object();
        CUTF16String v = W("value");
        ob_set_a((PA_ObjectRef)o, L"k", &v);
        bool ok = check(text_prop(o, "k") == W("value"), "value");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_set_p_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_p((PA_ObjectRef)o, L"pic", (PA_Picture)new_picture()); /* helper takes the picture */
        bool ok = check(g_pictures.size() == 1, "only the object's copy of the picture is live");
        release_object(o);
        return no_leaks() && ok;
    };

    /* Dormant defects: PA_GetObjectProperty results are owned and never
       cleared (text copies, wrong-kind values, retained object references). */
    t["defect_ob_get_s_result_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "hello");
        CUTF8String out;
        bool ok = check(ob_get_s((PA_ObjectRef)o, L"k", &out), "defined");
        ok = check(out == CUTF8String((const uint8_t *)"hello"), "value") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_a_result_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "hello");
        CUTF16String out;
        bool ok = check(ob_get_a((PA_ObjectRef)o, L"k", &out), "defined");
        ok = check(out == W("hello"), "value") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_d_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "d", "2026-11-12");
        short dd = 0, mm = 0, yy = 0;
        bool ok = check(!ob_get_d((PA_ObjectRef)o, L"d", &dd, &mm, &yy), "wrong kind is not a date");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_b_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "flag", "true");
        bool ok = check(ob_get_b((PA_ObjectRef)o, L"flag") == false, "wrong kind reads as false");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_n_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "n", "1");
        bool ok = check(ob_get_n((PA_ObjectRef)o, L"n") == 0.0, "wrong kind reads as 0");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_o_reference_not_released"] = [] {
        Obj *o = new_object();
        Obj *child = new_object();
        put_ref(o, "child", child);
        bool ok = check(ob_get_o((PA_ObjectRef)o, L"child") == (PA_ObjectRef)child, "returns the child");
        ok = check(child->rc == 1, "child reference borrowed from parent") && ok;
        put_text(o, "s", "x");
        ok = check(ob_get_o((PA_ObjectRef)o, L"s") == 0, "wrong kind returns NULL") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_c_reference_not_released"] = [] {
        Obj *o = new_object();
        Obj *child = new_object(true);
        put_ref(o, "items", child);
        bool ok = check(ob_get_c((PA_ObjectRef)o, L"items") == (PA_CollectionRef)child, "returns the collection");
        ok = check(child->rc == 1, "collection reference borrowed from parent") && ok;
        put_text(o, "s", "x");
        ok = check(ob_get_c((PA_ObjectRef)o, L"s") == 0, "wrong kind returns NULL") && ok;
        release_object(o);
        return no_leaks() && ok;
    };

    /* Guards (contract hardening, not confirmed engine defects): the result of
       a failed engine call is neither read nor cleared. */
    t["guard_get_error_result_untouched"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        g_fail_get = true;
        CUTF8String s;
        CUTF16String a;
        short dd = 0, mm = 0, yy = 0;
        bool ok = check(!ob_get_s((PA_ObjectRef)o, L"k", &s), "ob_get_s fails");
        ok = check(!ob_get_a((PA_ObjectRef)o, L"k", &a), "ob_get_a fails") && ok;
        ok = check(!ob_get_d((PA_ObjectRef)o, L"k", &dd, &mm, &yy), "ob_get_d fails") && ok;
        ok = check(!ob_get_b((PA_ObjectRef)o, L"k"), "ob_get_b fails") && ok;
        ok = check(ob_get_n((PA_ObjectRef)o, L"k") == 0.0, "ob_get_n fails") && ok;
        ok = check(!ob_get_o((PA_ObjectRef)o, L"k"), "ob_get_o fails") && ok;
        ok = check(!ob_get_c((PA_ObjectRef)o, L"k"), "ob_get_c fails") && ok;
        g_fail_get = false;
        ok = check(s.empty() && a.empty(), "failed result was not read") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["guard_ob_stringify_error_result_untouched"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        g_fail_command = true;
        CUTF8String json;
        ob_stringify((PA_ObjectRef)o, &json);
        g_fail_command = false;
        bool ok = check(json.empty(), "failed result was not read");
        release_object(o);
        return no_leaks() && ok;
    };

    /* Controls: must pass on both baseline and fixed sources. */
    t["control_cmd_lisp_values"] = [] {
        /* functional only: the live leak above makes a leak check fail here */
        ecl_stub_configure(kOutU32, U"warn", true);
        Obj *r = run_lisp(&kSourceU16);
        bool ok = check(ecl_stub_last_source() == kSourceU32, "source converted UTF-16 -> UTF-32");
        ok = check(ecl_stub_eval_calls() == 1, "evaluated once") && ok;
        ok = check(bool_prop(r, "success") == 1, "success") && ok;
        ok = check(text_prop(r, "stdout") == kOutU16, "stdout") && ok;
        ok = check(text_prop(r, "stderr") == W("warn"), "stderr") && ok;
        release_object(r);
        return ok;
    };
    t["control_ob_set_scalars"] = [] {
        Obj *o = new_object();
        ob_set_n((PA_ObjectRef)o, L"n", 1.5);
        ob_set_n((PA_ObjectRef)o, "n2", 2.5);
        ob_set_i((PA_ObjectRef)o, L"i", 7);
        ob_set_b((PA_ObjectRef)o, L"b", true);
        ob_set_0((PA_ObjectRef)o, L"z");
        ob_set_0((PA_ObjectRef)o, "z2");
        bool ok = check(o->props.size() == 6, "six properties");
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_set_o_c_transfer"] = [] {
        Obj *o = new_object();
        Obj *a = new_object(), *b = new_object(), *c = new_object(true), *d = new_object(true);
        ob_set_o((PA_ObjectRef)o, L"a", (PA_ObjectRef)a);
        ob_set_o((PA_ObjectRef)o, "b", (PA_ObjectRef)b);
        ob_set_c((PA_ObjectRef)o, L"c", (PA_CollectionRef)c);
        ob_set_c((PA_ObjectRef)o, "d", (PA_CollectionRef)d);
        bool ok = check(a->rc == 1 && b->rc == 1 && c->rc == 1 && d->rc == 1, "parent holds the only references");
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_get_scalar_kinds"] = [] {
        Obj *o = new_object();
        put(o, "x", real_var(2.25));
        put(o, "f", bool_var(true));
        put(o, "d", date_var(12, 11, 2026));
        bool ok = check(ob_is_defined((PA_ObjectRef)o, L"x"), "defined");
        ok = check(!ob_is_defined((PA_ObjectRef)o, L"y"), "undefined") && ok;
        ok = check(ob_get_n((PA_ObjectRef)o, L"x") == 2.25, "real") && ok;
        ok = check(ob_get_b((PA_ObjectRef)o, L"f") == true, "bool") && ok;
        short dd = 0, mm = 0, yy = 0;
        ok = check(ob_get_d((PA_ObjectRef)o, L"d", &dd, &mm, &yy) && dd == 12 && mm == 11 && yy == 2026, "date") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_stringify"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        put(o, "n", real_var(1));
        CUTF8String json;
        ob_stringify((PA_ObjectRef)o, &json);
        bool ok = check(json == CUTF8String((const uint8_t *)"{2}"), "stringified");
        release_object(o);
        return no_leaks() && ok;
    };
    return t;
}

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    std::map<std::string, Test> all = tests();
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(auto &kv : all) printf("%s\n", kv.first.c_str());
        return 0;
    }
    if(argc != 2 || !all.count(argv[1])) {
        fprintf(stderr, "usage: %s --list | <test>\n", argv[0]);
        return 2;
    }
    return all[argv[1]]() ? 0 : 1;
}
