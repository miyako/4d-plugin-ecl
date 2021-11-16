![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-ecl)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-ecl/total)

# 4d-plugin-ecl
Use [ECL (Embeddable Common-Lisp)](https://common-lisp.net/project/ecl/main.html) in 4D.

### Usage

```4d
$lisp:="(format t \"こんにちは せかい\")\")"

$status:=lisp ($lisp)
```

#### 参考記事

* [fib_table.h throws a precompiler error](https://gitlab.com/embeddable-common-lisp/ecl/-/issues/213)
* [Windows10でECLをビルド](https://qiita.com/NOEU/items/3a6ad5405a8cf5a66741)
