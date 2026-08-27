#ifndef INIMERSE_PLATFORM_PAL_FEATURES_H
#define INIMERSE_PLATFORM_PAL_FEATURES_H

/* Compile-time capability flags. */
#if defined(_WIN32)
#define IM_PLATFORM_WINDOWS 1
#define IM_HAS_GUI 1
#define IM_HAS_SERIAL 1
#define IM_HAS_NATIVE_INPUT 1
#define IM_HAS_PE_EMBED 1
#define IM_HAS_WINHTTP 1
#else
#define IM_PLATFORM_POSIX 1
#define IM_HAS_GUI 0
#define IM_HAS_SERIAL 0
#define IM_HAS_NATIVE_INPUT 0
#define IM_HAS_PE_EMBED 0
#define IM_HAS_WINHTTP 0
#endif

#endif
