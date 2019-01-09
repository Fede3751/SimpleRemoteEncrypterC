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

#define QUEUE_MAX_LENGTH 65536

server_configuration conf;

int main(int argc, char *args[]) {

	conf.run = 1;

	while(conf.run) {

		//display a welcome message, different if you're running on Unix or Windows
		if(!conf.restart)
			welcome_message();

		//configure application parameters
		server_read_and_set_arguments(argc, args, &conf);

		if(conf.no_threads == 0) {
			printf("Error: number of threads must be at least one!\nApplication will now close...\n\n");
			exit(1);
		}

		//call start-up function, this will be a different implementation wether the program is running either on Unix or Windows
		//For unix: application will be started on a daemon process, becoming invisible to the user
		//For windows: application will launch normally (empty function which always returns 0)
		//Startup may also be called (under Unix system) to relaunch the application and read configuration file again. FREE MEMORY, CLOSE SOCKET AND JOIN ALL THREADS BEFORE CALLING IT!
		
		//NOTE: DAEMON-STARTUP IS DISABLED WHILE DEBUGGING (HOW CAN YOU DEBUG A DAEMON APPLICATION?)
		if(startup(conf.directory) != 0) {
			printf("There was an error while trying to start the server, please retry...\n\n");
			exit(1);
		}

		//allocate space for listeners, so that can be joined later
		thread *saved_listeners;

		if((saved_listeners = (thread *)malloc(conf.no_threads * sizeof(thread))) == NULL) {
			printf("There was an erorr while trying to allocate resources for the application, please retry...\n\n");
			exit(1);
		}

		//allocate space for socket_interface so that other threads can read it
		io_interface *sock_ptr;

		if ((sock_ptr = (io_interface *)malloc(sizeof(io_interface))) == NULL) {
			printf("There was an error while trying to allocate resources for the application, please retry...\n\n");
			exit(1);
		 }

		//start server and listen on the chosen port	
		if(host_server(conf.port, sock_ptr) < 0) {
			printf("Error while trying to host server on port %i\n\n", conf.port);
			exit(1);
		}


		io_interface_queue *accepted_socks			= malloc(sizeof(io_interface_queue));
		semaphore *remaining_accept				= malloc(sizeof(semaphore));
		semaphore *sem						= malloc(sizeof(semaphore));

		if(accepted_socks == NULL || remaining_accept == NULL || sem == NULL) {
			printf("Error while trying to allocate space for queue!\n\n");
			exit(1);
		}

		bzero(accepted_socks, sizeof(io_interface_queue));

		start_semaphore_ex(sem);
		start_semaphore(remaining_accept, 0, QUEUE_MAX_LENGTH);

		//allocate space for listener conf
		listener_job *job		= malloc(sizeof(listener_job));
		job->queue			= accepted_socks;
		job->rr 			= remaining_accept;
		job->sem			= sem;
		job->restart 			= &conf.restart;
		
		 

		//start all listening threads
		if(start_listeners(conf.no_threads, job, saved_listeners) != 0) {
			printf("Error while trying to create new threads, please retry...\n\n");
			exit(1);
		}


		io_interface accepted_sock;

		while(!conf.restart) {

			if(listen_to_sock_non_block(sock_ptr, &accepted_sock, 2) == 0) {
				semaphore_wait(job->sem);

				//allocate space for queue node. It will be freed by a listener after it has been used
				io_interface_node *temp;
				if ((temp = malloc(sizeof(io_interface_node))) == NULL) {
					printf("Error allocating resources for a client, request will be discarded...\n");
					semaphore_signal(job->sem);
					continue;
				}

				temp->current = accepted_sock;

				enqueue(job->queue, temp);
				semaphore_signal(job->sem);
				semaphore_signal(job->rr);

			}
		}

		//fake signal to wake up every thread and let them join to this thread later
		for (int i = 0; i < conf.no_threads; i++) {
			semaphore_signal(job->rr);
		}

		//close socket
		close_socket(sock_ptr);
		printf("\tSocket closed, requests from port %i are no longer accepted!\n", conf.port);

		
		//join all threads
		printf("\tWaiting for every thread to finish its task...\n");
		for(int i=0; i<conf.no_threads; i++)
			join_thread(&saved_listeners[i], NULL);
		if (conf.run)
			printf("\tDone! Now restarting...\n\n");
		else
			printf("\tDone! Now closing application...\n\n");
		
		//stop semaphores
		stop_semaphore(sem);
		stop_semaphore(remaining_accept);

		//free space before closing
		free(saved_listeners);
		free(sock_ptr);
		free(accepted_socks);
		free(remaining_accept);
		free(sem);
		free(job);

	}		

	free(conf.starting_directory);
	free(conf.directory);


	//end
	return 0;

}


void restart_application(int s) {
	conf.restart = 1;
}

void stop_application(int s) {
	printf("\b\b  \b\b");
	conf.run = 0;
	conf.restart = 1;
}