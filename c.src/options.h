#ifndef _edf_options_
#define _edf_options_ 1

// if defined, pages that have been marked as dirty will have checksums
// written to them.
#define EDB_OPT_CHECKSUMS

// Adds an a lot of extra code who's only purpose is to "triple check"
// everything as it goes in and out. Theoretically this code is never used.
// This code is only to check the integretty of the developer rather than
// the user/maintainer. It will be turned off in releases as it is expected to slow
// down a lot
#define EDB_FUCKUPS

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