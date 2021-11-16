//%attributes = {}
$lisp:="(format t \"こんにちは せかい\")\")"

$status:=lisp ($lisp;$options)

/*

options: not implemented (yet)

possible scenario:

stdout (path)
stderr (path)

ecl_set_option
https://common-lisp.net/project/ecl/static/manual/Embedding-ECL.html#tab_003aboot_002doptions

*/