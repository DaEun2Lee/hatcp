#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "app.h"
#include "utils.h"
#include <ff_api.h>

int
main(int argc, char* argv[])
{
	int error;
	struct wanacc_app app;

	error = init_app(&app);
	if (error) {
		APPERR("Cannot initialize app.\n");
		exit(0);
	}

	error = parse_args(&app, argc, argv);
	if (error) {
		APPERR("Cannot parse user arguments.\n");
		usage();
		exit(0);
	}

	char *ff_argv[] = {
	        argv[0],
	        "-c", "/data/f-stack-hatcp/config.ini",
	        "-p", "0",
	        NULL
	};
	ff_init(5, ff_argv);

	DBG("wanacc pid:%u", getpid()); 

	while (1) {
		start_app(&app);
		clean_app(&app);
	}

	return (0);
}
