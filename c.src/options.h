#ifndef _edf_options_
#define _edf_options_ 1


// COMPILER-DEFINED. The following macros will always be commented out here.
// These are defined by the compiler. Note to self: do NOT use these macros
// outside of this file. Instead, use these macros to turn on other, more
// specific macros inside of this options file. In otherwords: we delegate
// the compiler-defined macros to other macros.
//
// All of these macros will start with _ODB_CD_* where as the "CD" stands for
// Compiler-Defined. The leading underscore will deter use outside of this file.
//
// _ODB_CD_RELEASE
//     The compiler will define
//     this signifying that the build process is going through and building a
//     "production-ready" or release build.
//     #define _ODB_CD_RELEASE
//
// _ODB_CD_VERSION
//     This will be a string literal that denotes the "version"
//     of the build.

// if defined, pages that have been marked as dirty will have checksums
// written to them.
#define EDB_OPT_CHECKSUMS

// EDB_FUCKUPS
//     Adds an a lot of extra code who's only purpose is to "triple check"
//     everything as it goes in and out. Theoretically this code is never used.
//     This code is only to check the integretty of the developer rather than
//     the user/maintainer. It will be turned off in releases as it is expected
//     to slow down a lot.
//
//     Never to be compiled in release builds.
#ifndef _ODB_CD_RELEASE
  #define EDB_FUCKUPS
#endif

// If EDB_OPT_PRA_OS is defined, then it will use the operating system's
// native page replace algo. Otherwise, it will use a proprietary oidadb one.
//#define EDB_OPT_PRA_OS


// Define to enable odbtelemtry at all
#define EDBTELEM
#ifdef EDBTELEM
// log_debug message will be generated every telemetry message. Note: will
// REALLY slow everything down. Like badly. Use only in desperate situations
//#define EDBTELEM_DEBUG

// Enable/disable inner-process telemetry.
#define EDBTELEM_INNERPROC // todo: remove
#endif



#endif