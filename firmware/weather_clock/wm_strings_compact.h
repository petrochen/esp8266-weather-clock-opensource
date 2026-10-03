#pragma once
// WiFiManager 2.0.17's supported string override. Retain every form, script and
// route; replace only its bitmap-heavy stylesheet. Upstream strings are MIT.
#define HTTP_STYLE WM_ORIGINAL_STYLE
#include <wm_strings_en.h>
#undef HTTP_STYLE
const char HTTP_STYLE[] PROGMEM =
  "<style>body{font:16px system-ui,sans-serif;text-align:center;color:#1f3544;background:#f3f5f7}"
  ".wrap{display:inline-block;text-align:left;width:100%;max-width:500px;overflow-wrap:anywhere}*{box-sizing:border-box}"
  "input,select,button{font:inherit;max-width:100%;padding:8px;margin:5px 0;min-height:44px}"
  "input,select,button{width:100%}input[type=checkbox],input[type=radio]{width:auto;min-height:0}"
  "button{background:#1e6085;color:white;border:0;border-radius:4px;cursor:pointer}a{color:#1e6085}"
  ".msg{border-left:3px solid;padding:12px;margin:12px 0}.D{color:#a22}.S{color:#275}.h{display:none}"
  "dt{font-weight:bold}dd{margin:0 0 8px}td{vertical-align:top}:disabled{opacity:.5}"
  ".q{float:right}.q.l:before{content:'Locked ';font-size:12px}.q-0:after{content:'0/4'}"
  ".q-1:after{content:'1/4'}.q-2:after{content:'2/4'}.q-3:after{content:'3/4'}.q-4:after{content:'4/4'}"
  ":focus-visible{outline:2px solid #1e6085;outline-offset:2px}</style>";
