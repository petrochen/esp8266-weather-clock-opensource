// Applied to the sketch, core and libraries by ESP8266 core 3.1.2.
// Keep the provisioning portal, omit WiFiManager's verbose serial diagnostics.
#define WM_NODEBUG
#define WM_NOHELP
// The core injects -I<build>/core; Arduino stages sketch headers in ../sketch.
#define WM_STRINGS_FILE "../sketch/wm_strings_compact.h"
