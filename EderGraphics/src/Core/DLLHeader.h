#pragma once

#ifdef EDERGRAPHICS_EXPORTS
#define EDERGRAPHICS_API __declspec(dllexport)
#else
#define EDERGRAPHICS_API __declspec(dllimport)
#endif