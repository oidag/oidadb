#include <unistd.h>
#include <stdio.h>
#include <oidadb/config.h>

odbc_configuration_t odbc_configuration = {
		.fd_critical = STDERR_FILENO,
		.fd_debug    = -1,
		.fd_error    = STDERR_FILENO,
		.fd_log      = STDOUT_FILENO,
		.fd_warn     = STDOUT_FILENO,
		.fd_notice   = STDOUT_FILENO,
		.fd_alert    = STDERR_FILENO,

};
