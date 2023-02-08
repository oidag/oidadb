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


// Define to enable odbtelemtry at all
#define EDBTELEM
#ifdef EDBTELEM
// log_debug message will be generated every telemetry message.
#define EDBTELEM_DEBUG
#endif

#endif