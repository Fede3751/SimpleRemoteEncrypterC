#ifdef _WIN32
	#include "win/io.c"
	#include "win/startup.c"
#endif

#ifdef __unix__
	#include "unix/io.c"
	#include "unix/startup.c"
#endif


#include "cross/queue.c"
#include "cross/requests.c"
#include "cross/startup.c"

client_configuration conf;

int main(int argc, char* args[]) {

	//display a welcome message, different if you're running on Unix or Windows
	welcome_message();
	
	//configure application parameters
	client_read_and_set_arguments(argc, args, &conf);


	//start action on a parallel thread
	thread action;

	if (create_thread(&action, client_startup, (void *)&conf) < 0) {
		printf("There was an errory while trying to start the application. Please retry...\n\n");
		exit(1);
	}

	//join given action
	join_thread(&action, NULL);

	//end
	return 0;	
}

void restart_application(int s) {}
void stop_application(int s) {}
