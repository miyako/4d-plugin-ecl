![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-ecl)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-ecl/total)

# 4d-plugin-ecl

The ECL plugin embeds [ECL (Embeddable Common-Lisp)](https://common-lisp.net/project/ecl/main.html) inside 4D, letting you evaluate arbitrary Common Lisp expressions from 4D code and get the result — specifically, whatever the expression printed to standard output and standard error — back as a 4D `Object`. The plugin exposes a single command, `lisp`, which boots one shared ECL runtime when the plugin loads and re-uses it for every call.

## Summary

| Command | Returns | Purpose |
|---|---|---|
| [`lisp`](#lisp) | `Object` | Evaluates a Common Lisp expression and returns its captured `stdout`/`stderr` output plus a success flag. |

**Platforms:** macOS (Intel & Apple Silicon), Windows 64-bit — 4D v18 or later.

---

## Requirements & platform notes

- The plugin has exactly one command, `lisp`, and it takes its Lisp source as `Text` — there's no separate "compile" or "load file" step; every call parses and evaluates the string you pass in.
- **Only the first Lisp form in the string is evaluated.** Internally the plugin reads one object from the string (equivalent to Common Lisp's `read-from-string`) and evaluates just that. Anything in the string after the first complete, balanced form is never looked at. If you need to run several top-level forms in one call, wrap them in a single `(progn ...)`.
- The second parameter (an `Object`) is accepted by the command's syntax but **is not currently used by the plugin** — see [Parameter 2](#lisp) below.
- There is no timeout and no way to interrupt an evaluation from 4D. A Lisp expression that never terminates (an infinite loop, an unbounded `loop` that keeps printing) will block the calling process indefinitely.
- The plugin is marked thread-safe in its manifest, but standard output/error capture is not isolated per call — see [Error handling & troubleshooting](#error-handling--troubleshooting) for what that means in practice.
- Text passed in and text returned is transcoded through UTF-8/UTF-32 internally, so non-ASCII source and output (accented characters, Japanese, emoji, etc.) round-trip correctly — this is exercised directly in the plugin's own example (see below).

---

## lisp

### Syntax

```4d
$status:=lisp(lispExpression; options)
```

| Parameter | Type | Description |
|---|---|---|
| `lispExpression` | Text | A Common Lisp expression (or a series of expressions inside a single `(progn ...)`) to read and evaluate. Mandatory — if omitted or empty, the command returns immediately with `success` set to `False` and an explanatory message in `stderr`. |
| `options` | Object | Reserved for future use. Currently ignored entirely by the plugin — pass an empty object, `Null`, or omit meaningful content; nothing you put in it affects the call today. The plugin's own test file notes possible future options such as redirecting `stdout`/`stderr` to a file path. |
| Result | Object | See [Result object](#result-object) below. |

### Result object

| Property | Type | Description |
|---|---|---|
| `success` | Boolean | `True` if the expression was read and evaluated without a Lisp error; `False` otherwise (including a missing/unparseable expression, or an internal error during evaluation). |
| `stdout` | Text | Everything the evaluated form wrote to Common Lisp's standard output stream, captured as a string. Empty text if nothing was printed. |
| `stderr` | Text | Everything written to standard error, and where relevant, condition/error information from a failed evaluation. Empty text if nothing was printed. |

### Description

`lisp` parses `lispExpression` with the Common Lisp reader and evaluates the resulting form inside the plugin's shared ECL runtime (one runtime is booted once, the first time the plugin loads, and reused for every subsequent call — you don't initialize it yourself). Whatever the form prints during evaluation is captured separately for standard output and standard error and returned as `stdout`/`stderr` text on the result object; the return *value* of the Lisp form itself is not returned to 4D — only what it printed, and whether evaluation succeeded.

Because only the first form in the string is read, a string containing `(format t "a") (format t "b")` evaluates just the `(format t "a")` call; the second call is never reached. To run both, write `(progn (format t "a") (format t "b"))` instead.

A failed evaluation (a malformed form, an unbound function, a Lisp-level error signaled during evaluation) is reflected as `success = False`; the plugin does not raise a 4D error or interrupt your method — check `success` (and `stderr` for detail) after every call rather than assuming it worked.

### Example

From the plugin's own test method (`TEST.4dm`):

```4d
//%attributes = {}
$lisp:="\r;;\r;;\r;;\r;;実行例\n(format t \"こんにちは せかい\")\")"

$status:=lisp($lisp; $options)
```

(The `;;` lines are Lisp comments — a label reading "実行例", Japanese for "usage example" — and the stray `\")` after the closing paren of the `format` call is inert trailing text in the string; since only the first form is read, it has no effect.)

A minimal, self-contained version of the same idea:

```4d
$lisp:="(format t \"Hello, world!\")"
$status:=lisp($lisp; New object)

If ($status.success)
	ALERT($status.stdout)
Else
	ALERT("Lisp error: "+$status.stderr)
End if
```

Running several statements in one call:

```4d
$lisp:="(progn "+\
	   "(format t \"Line 1~%\") "+\
	   "(format t \"Line 2~%\") "+\
	   "(format t \"2 + 2 = ~A\" (+ 2 2)))"

$status:=lisp($lisp; New object)
```

---

## Error handling & troubleshooting

- **`success` is `False` with no useful `stderr`** — check first whether `lispExpression` was actually non-empty; an omitted or empty parameter 1 returns immediately without attempting to evaluate anything.
- **Only part of a multi-statement expression seems to run** — this is expected: only the first form in the string is read and evaluated. Combine multiple statements into one `(progn ...)` form.
- **A call never returns / 4D appears frozen** — there is no built-in timeout or cancellation for a Lisp evaluation. An infinite loop or unbounded output-producing form in `lispExpression` will hang the calling process indefinitely; test any dynamically-built expression in isolation before using it in production, and avoid evaluating untrusted Lisp input.
- **Output from one call appears mixed into another's `stdout`/`stderr`** — the plugin's manifest marks `lisp` thread-safe, but its standard-output/error capture is implemented with a single, shared destination rather than one fully isolated per call. If you call `lisp` from more than one process or preemptive thread at the same time, don't rely on `stdout`/`stderr` from concurrent calls being kept perfectly separate. If accurate output capture matters, serialize calls to `lisp` (for example with a 4D semaphore) rather than firing them concurrently from multiple processes.
- **`options` doesn't seem to do anything** — correct, as of this release it's accepted but not read by the plugin at all; there is currently no way to redirect output or pass evaluation options through it.
- **Non-ASCII text looks garbled** — shouldn't happen under normal use (the plugin round-trips UTF-8/UTF-32 for both the expression and the captured output, as shown in the Japanese-text example above); if you do see mangled characters, first confirm the `Text` value you're passing in isn't already mis-encoded before it reaches `lisp`.

---

## Quick reference

```4d
// simplest call
$status:=lisp("(format t \"Hello, world!\")"; New object)
If ($status.success)
	ALERT($status.stdout)
End if

// multiple statements: wrap in (progn ...)
$status:=lisp("(progn (format t \"a\") (format t \"b\"))"; New object)

// always check success before trusting stdout
If (Not($status.success))
	ALERT("Lisp error: "+$status.stderr)
End if
```
