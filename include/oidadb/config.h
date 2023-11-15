#ifndef OIDADB_CONFIG_H
#define OIDADB_CONFIG_H

/*
 * This configuration file is for library-wide configuration. These values are only designed to be adjusted on
 * process start-up. These deal mainly with "politics".
 */

typedef struct {

	// if true, critical errors will be sent to the maintainer
	int f_reportcritical;

	//char license[128];

	//char maintainerhost[128]


	// these all will be initialized as -1
	int fd_critical;
	int fd_error;
	int fd_log;
	int fd_debug;
	int fd_warn;
	int fd_notice;
	int fd_alert;
} odbc_configuration_t;


// this is initialized.
extern odbc_configuration_t odbc_configuration;

#endif //OIDADB_CONFIG_H
