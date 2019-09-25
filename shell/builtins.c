#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>


#include "builtins.h"

int lexit(char*[]);
int cd(char*[]);
int lkill(char*[]);
int ls(char*[]);
int echo(char*[]);
int undefined(char *[]);

builtin_pair builtins_table[]={
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&cd},
	{"lkill",	&lkill},
	{"lls",		&ls},
	{"cd",		&cd},
	{NULL,NULL}
};

void err(char* com) {
	char str[strlen(com)+16];
	sprintf(str, "Builtin %s error.\n", com);
	write(2, str, strlen(str));
}


int 
echo( char* argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int
lexit( char* argv[] )
{
	if ( argv[1] == NULL ) {
		exit(0);
	}
	if ( argv[2] != NULL ) {
		err(argv[0]);
		return -1;
	}

	int exit_code = 0;
	if ( sscanf(argv[1], "%d", &exit_code) > 1) {
		err(argv[0]);
		return -1;
	}
	exit(exit_code);

	err(argv[0]);
	return -1;
}

int
cd( char* argv[] )
{
	if ( argv[1] == NULL ) {
		int res = chdir(getenv("HOME"));
		if(res == -1) {
			err(argv[0]);
			return -1;
		}
		return 0;
	}
	if ( argv[2] != NULL ) {
		err(argv[0]);
		return -1;
	}

	int res = chdir(argv[1]);
	if(res == -1) {
		err(argv[0]);
		return -1;
	}
	return 0;
}

int
lkill( char * argv[] )
{
	if ( argv[1] == NULL ) {
		err(argv[0]);
		return -1;
	}
	
	if ( argv[2] != NULL && argv[3] != NULL ) {
		err(argv[0]);
		return -1;
	}

	int pid, sig;
	char test = 0;
	if ( argv[2] == NULL ) {
		sig = SIGTERM;
	}
	else if (sscanf(argv[1], "-%d%c", &sig, &test) < 1 || test != 0) {
		err(argv[0]);
		return -1;
	}
	if ( sscanf(argv[2], "%d%c", &pid, &test) < 1 || test != 0) {
		err(argv[0]);
		return -1;

	}
	int res = kill( (pid_t) pid, sig);
	if(res == -1) {
		err(argv[0]);
		return -1;
	}
	return 0;
}

int
ls( char * argv[] )
{

	if ( argv[1] != NULL && argv[2] != NULL ) {
		err(argv[0]);
		return -1;
	}

	DIR *dir;
    struct dirent *dp;
	
	if ( argv[1] == NULL ) {
		argv[1] = ".";
	}

	if ((dir = opendir (argv[1])) == NULL) {
	    err(argv[0]);
	    return -1;
	}

	while( (dp = readdir(dir)) != NULL ) {
	   	if(dp->d_name[0]!='.') {
	    	char str[strlen(dp->d_name)+1];
	    	sprintf(str, "%s\n", dp->d_name);
	    	int w = 0;
    		while (true) {
				w += write(1, str, strlen(str) - w);
				if (w == strlen(str) || errno != EINTR) {
					break;
				}
			} 
	    }
	}

	if (dp != NULL) {
	   	err(argv[0]);
	    return -1;
	}

	if( closedir(dir) == -1) {
		err(argv[0]);
	    return -1;	
	}
	return 0;
}

int 
undefined(char * argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}
