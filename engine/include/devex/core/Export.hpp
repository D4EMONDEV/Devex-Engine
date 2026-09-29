#pragma once

// What the engine library shares with the programs, tests and game modules that link it: the
// classes and functions of its headers, marked DEVEX_API; the rest stays inside it. The engine is
// built with DEVEX_BUILDING_ENGINE, which exports them; everything else imports them.
#if defined(_WIN32)
#    if defined(DEVEX_BUILDING_ENGINE)
#        define DEVEX_API __declspec(dllexport)
#    else
#        define DEVEX_API __declspec(dllimport)
#    endif
#else
#    define DEVEX_API __attribute__((visibility("default")))
#endif
