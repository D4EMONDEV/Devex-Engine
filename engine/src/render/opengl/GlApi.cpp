#include "GlApi.hpp"

namespace devex::render::opengl::gl {

const char* loadFunctions(void* (*find)(const char* name))
{
    const char* missing = nullptr;
#define DEVEX_GL_LOAD_REQUIRED(name, result, parameters)                                                                           \
    name = reinterpret_cast<result(DEVEX_GL_CALL*) parameters>(find("gl" #name));                                                  \
    if (name == nullptr && missing == nullptr)                                                                                     \
    {                                                                                                                              \
        missing = "gl" #name;                                                                                                      \
    }
#define DEVEX_GL_LOAD_OPTIONAL(name, result, parameters) name = reinterpret_cast<result(DEVEX_GL_CALL*) parameters>(find("gl" #name));
    DEVEX_GL_FUNCTIONS(DEVEX_GL_LOAD_REQUIRED, DEVEX_GL_LOAD_OPTIONAL)
#undef DEVEX_GL_LOAD_REQUIRED
#undef DEVEX_GL_LOAD_OPTIONAL
    return missing;
}

} // namespace devex::render::opengl::gl
