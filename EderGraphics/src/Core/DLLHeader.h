#pragma once

#ifdef _WIN32
    #ifdef EDERGRAPHICS_EXPORTS
        #define EDERGRAPHICS_API __declspec(dllexport)
    #else
        #define EDERGRAPHICS_API __declspec(dllimport)
    #endif
#else
    #define EDERGRAPHICS_API __attribute__((visibility("default")))
#endif
