#include <signal.h>
#include <string.h>

#define DEBUGGING 1

void welcome_message() {
       printf("\nProgrammazione di Sistema - Progetto 2016/17 - Unix Build\n\n");
}



void restart_application(int s);
void stop_application(int s);

/*
* Start the application, Unix implementation. Save all the configuration specifications on the given startup structure
*/
int startup(char *dir) {

	struct sigaction reset_action;
	reset_action.sa_handler = restart_application;
	struct sigaction stop_action;
	stop_action.sa_handler = stop_application;
	struct sigaction ignore_action;
	ignore_action.sa_handler = SIG_IGN;

	
	if( DEBUGGING ) {
 		if(chdir(dir) < 0) {
			printf("Error while trying to set up given directory!\nApplication will now close...\n\n");
			exit(0);
		}
		sigaction(SIGHUP, &reset_action, NULL);
		sigaction(SIGINT, &stop_action, NULL);
		sigaction(SIGPIPE, &ignore_action, NULL);
		printf("Server launched in debugging mode. Process will stay attached to this console. Enjoy your stay!\n\n");
		return 0;
	}

	printf("Server will now go on daemon mode and will be detached from this console. Have a good day!\n\n");
	
	pid_t pid = fork();

	//error while trying to fork
	if(pid < 0)
		return -1; 
	//parent process, exit successfully
	if (pid > 0)
		exit(0);
	//make child process the session leader
	if (setsid() < 0)
		return -1;
	//ignore SIGHUP signal

	sigaction(SIGHUP, &reset_action, NULL);
	sigaction(SIGINT, &stop_action, NULL);
	sigaction(SIGPIPE, &ignore_action, NULL);
	
	//fork again
	pid = fork();

	if(pid < 0)
		return -1;
	if(pid > 0)
		exit(0);
	
	if(chdir(dir) < 0)
		exit(1);

	//close all fd, so that this child process will be detached from the console and become a real daemon process
	int x;
	for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
		close (x);
	}

	return 0;
}

