#include "include/ellemdb.h"


// todo: analytics_pagefault(), analytics_trashfault(), ect... stuff for dashboard
// nothing should error.

// called when new lookup/object pages are successfully created AND referenced.
void analytics_newlookuppages(unsigned int entryid, edb_pid pid);
void analytics_newobjectpages(unsigned int entryid, edb_pid startpid, int straitc);