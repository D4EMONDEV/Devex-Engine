// A shared library that is not a game module: it exports a function, but no DEVEX_GAME_MODULE entry
// point, and does not use the engine.
#if defined(_WIN32)
#    define DEVEX_TEST_EXPORT __declspec(dllexport)
#else
#    define DEVEX_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" DEVEX_TEST_EXPORT int devexTestPlainFunction()
{
    return 7;
}
