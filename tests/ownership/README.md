# Ownership regressions

A synthetic-boundary harness for 4D native value ownership in this plugin's
own code (`4DPlugin-ECL.cpp`, `support/4DPlugin-JSON.cpp`).

- The real plugin sources and the **unmodified** bundled 4D Plugin API
  (`4D Plugin API/4DPluginAPI.c`) are compiled with ASan/UBSan.
- Only the engine callback (`gCall4D`) is replaced. `engine_harness.cpp`
  models engine strings, pictures, objects, collections and handles with
  tracked `malloc`/`free`, so leaks, double frees and foreign disposals are
  detected exactly.
- ECL is replaced by `ecl_stub.cpp`. `libecl` is not linked, and no Lisp is
  read or evaluated: the stub returns configured stdout/stderr text and
  evaluation outcomes.
- The `lisp` command is driven through the real `PluginMain` entry point.

```sh
# fixed tree: every test must pass
tests/ownership/run.sh . fixed

# immutable baseline: named defects must fail, controls must pass
D=$(mktemp -d); git archive <base-sha> | tar -x -C "$D"
tests/ownership/run.sh "$D" baseline
```

## Contracts covered

- `PA_CreateVariable(eVK_Unistring/eVK_Picture)` allocates an empty value.
  A shallow `PA_SetStringVariable`/`PA_SetPictureVariable` would overwrite
  and leak it, so the helpers create with `eVK_Undefined` and set once.
- `PA_SetObjectProperty` copies the value; the caller still clears its
  variable. `ob_set_p` and `ob_set_o`/`ob_set_c` take ownership of the value
  passed in.
- `PA_GetObjectProperty` returns an owned value (a copy, or a retained
  object/collection reference), which the getters now clear. As a result,
  `ob_get_o`/`ob_get_c` return a reference borrowed from the parent object.
- A failed `PA_GetObjectProperty`/`PA_ExecuteCommandByID` result is not
  initialised. It is neither read nor cleared unless `PA_GetLastError()` is
  `eER_NoErr`.

## Tests

- `defect_cmd_lisp_*`: live. Every `lisp` call stored stdout/stderr through
  `ob_set_s`, leaking one empty string per property.
- `defect_ob_set_*` and `defect_ob_get_*`: dormant helpers with no caller in
  this plugin.
- `guard_*`: hardening against reading/clearing failed results.
- `control_*`: must pass on both baseline and fixed sources.

## Not covered

- Native Windows builds (the `MultiByteToWideChar`/`WideCharToMultiByte`
  branches).
- A real 4D engine or a real ECL runtime.
- Apple clang has no LeakSanitizer, so leaks are detected by the engine
  model's live-resource accounting.
