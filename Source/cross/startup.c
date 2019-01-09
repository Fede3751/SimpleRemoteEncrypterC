#include <string.h>

//define actions so that there's no need to remember int = action association
#define LIST_ACTION		1
#define LIST_REC_ACTION 	2
#define ENC_ACTION		3
#define DEC_ACTION		4


#define LSTF_REQ		"LSTF"
#define LSTR_REQ		"LSTR"
#define ENCR_REQ		"ENCR"
#define DECR_REQ		"DECR"


#define FIN_MSG			200
#define MORE_MSG 		300
#define ERR_MSG			400
#define BUSY_MSG		500


#define LISTEN_MAX_TRIES	6 

#define CLIENT_LOG_FILE		"client.log"
#define DEFAULT_CONF		"server.conf"
#define DEFAULT_THREADS_NO	4
#define MAX_PATH_LENGTH		4096
#define DEFAULT_PORT		8888



/**
* Server configuration structure. Used by both implementation.
* It defines most of the parameters which are set every time the server starts (or restarts when receiving SIGHUP).
* This structure should be saved in a global scope so that every function can access to it easily without having to
* pass it every time its address.
*/
typedef struct {
	int port;
	int no_threads;
	char *directory;
	int run;
	int restart;
	char *starting_directory;
} server_configuration;


/**
* Client configuration structure. Used by both implementation.
* It defines most of the parameters which are set every time the client starts.
* This structure should be saved in a global scope so that every function can access to it easily without having to
* pass it every time its address.
*/
typedef struct {
	char *address;
	int port;
	int action;
	char *target;
	unsigned int seed;
} client_configuration;



/*
* Structure which defines a listener job configuration. Io contains:
*	-queue:		pointer to a queue of interfaces. A main thread should accept some calls, write it on this queue
*			and one or more listener threads will read from it
*	-rr: 		pointer to an integer which counts the remaining requests
*	-sem:		mutex semaphore to coordinate multi-thread access to the two variables just described
*
* ACCESS TO THIS POINTERS SHOULD ALWAYS BE UNDER A MUTEX SECTION! Use *sem to see if access is allowed and *rr to wait for queue to be filled!
*/
typedef struct {
	io_interface_queue		*queue;
	semaphore			*rr;
	semaphore			*sem;
	int 				*restart;
} listener_job;



/*
* Function used to parse a string to an integer. Implementation for both Linux and Windows.
* ARGUMENTS:
*	-source:	pointer to the string to parse
* RETURN VALUE:
*	The parsed number
*/
int parse_int(char *source) {
	return (int)strtol(source, (char **)NULL, 10);
}


/*
* Function used to parse a string to an unisgned int. Implementation for both Linux and Windows.
* ARGUMENTS:
*	-source:	pointer to the string to parse
* RETURN VALUE:
*	The parsed number
*/
unsigned int parse_int_unsigned(char *source) {
	return (unsigned int)strtoul(source, (char **)NULL, 10);
}


/*
* Function used to log a client encrypt request to the given file.
* ARGUMENTS:
*	-seed:		seed of the encrypt action used
*	-path:		path of the encrypted target
* RETURN VALUE:
*	On succes 0 is returned, otherwise -1
*/
int log_action(unsigned int seed, char *path) {

	FILE *log = fopen(CLIENT_LOG_FILE, "a");

	if(log == NULL)
		return -1;

	if(fprintf(log, "%10u\t%s\n", seed, path) < 0)
		return -1;

	if(fclose(log) < 0)
		return -1;

	return 0;

}

/*
* Function used to read a line from a mapped file. Every line is split when \n is found but if \r\n is found
* at the end of the line then \r is omitted.
* ARGUMENTS:
*	-target:	the mapped file which wants to be read
*	-dest:		the char pointer to save the result to
*	-line:		the number of the line to read
* RETURN VALUE:
*	On succes 0 is returned and dest is correctly set, otherwise -1
*/
int read_line(mapped_file *target, char *dest, int line) {

	char *index = target->id;


	//first find starting index!
	while(line > 0) {
		char *n_pointer = strchr(index, '\n');
		char *r_pointer = strchr(index, '\r');

		//no more lines to read! Don't modify index
		if(n_pointer == NULL && r_pointer == NULL)
			break;

		//set new index to the position after the most far \n or \r
		index = n_pointer+1;

		line--;
	}

	//the file does not reach the requested line
	if(line > 0)
		return -1;

	//now find ending index (which can be either another \n \r or the end of file)
	char *n_pointer = strchr(index, '\n');
	char *r_pointer = strchr(index, '\r');

	char *end_index;

	//check if this is the last line
	if(n_pointer == NULL && r_pointer == NULL) {
		end_index = target->id + target->size;
		if(index == end_index)
			return -1;
	}
	else {
		//this time take the smaller one (so that neither \r nor \n are included in the string), but only if it isn't NULL
		end_index = n_pointer < r_pointer ? 
			n_pointer != NULL ? n_pointer : r_pointer 
			: 
			r_pointer != NULL ? r_pointer : n_pointer;
	}

	//copy the string to the destination and put a string termination char at the end of it
	memcpy(dest, index, (int)(end_index - index)); 
	dest[end_index - index] = '\0';

	return 0;
}


/*
* Function used to read configuration file.
* ARGUMENTS:
*	-file:		file name to read configuration from
*	-target:	server_configuration variable to save the result to
* RETURN VALUE:
*	On success 0 is returned and target is correctly set, otherwise -1
*/
int read_from_file(char *file, server_configuration *target) {

	//map configuration file to memory
	mapped_file mapped;
	
	if(map_file_to_memory(file, &mapped) < 0)
		return -1;

	//now read every line and set configuration parameters properly
	char *line = malloc(MAX_PATH_LENGTH);
	int i=0;

	while(read_line(&mapped, line, i++) == 0) {

		switch(line[0]) {

			case 'p':
				target->port = parse_int(line + 1);
				break;
			case 'n':
				target->no_threads = parse_int(line + 1);
				break;
			case 'c':
				target->directory = malloc(MAX_PATH_LENGTH);
				strcpy(target->directory, line+2);
				break;
		}
	}
	
	free(line);

	unmap_file_from_memory(&mapped);

	return 0;
}


/*
* Function called by the server during startup and restart.
* It reads parameters from console if it is the first time the application is starting and
* the missing parameters from configuration file.
* On restart it reads only parameters from the configuration file.
*/
int server_read_and_set_arguments(int argc, char *args[], server_configuration *target) {

	//just read from file new parameters, ignore args
	if(target->restart) {

		if(chdir(target->starting_directory) < 0) {
			printf("There was an error while trying to restart: could not read starting directory.\nApplication will now close...\n\n");
			exit(1);
		}
		//set to zero restart variable (as restart is actually happening)
		target->restart = 0;
		//read new configuration
		server_configuration conf_from_file;
		conf_from_file.port = 0;
		conf_from_file.no_threads = 0;
		conf_from_file.directory = 0;

		if (read_from_file(DEFAULT_CONF, &conf_from_file) < 0) {
			printf("Could not read configuration file when restarting!\nApplication will now close...\n\n");
			exit(1);
		}

		if(conf_from_file.port != 0)
			target->port = conf_from_file.port;
		if(conf_from_file.no_threads != 0)
			target->no_threads = conf_from_file.no_threads;
		if(conf_from_file.directory != 0) {
			free(target->directory);
			target->directory = conf_from_file.directory;
		}

	}
	//this is actually the first time the application is starting, so give priority to args and then read from file
	else {

		char *start_dir = malloc(MAX_PATH_LENGTH);
		
		if(getcwd(start_dir, MAX_PATH_LENGTH) == 0) {
			printf("Error while trying to start the application: could not read starting directory!\nApplication will now close...\n\n");
			exit(1);
		}

		target->starting_directory = start_dir;

		target->restart = 0;

		int read_arguments 	= 1;
		int directory_set 	= 0;
		int port_set		= 0;
		int no_threads_set 	= 0;
		
		while (read_arguments < argc) {
	                
			if (strcmp(args[read_arguments], "-c") == 0) {
	                        
				printf("\tDirectory set to:\t\t\t\t\t%s\n", args[read_arguments+1]);
				
				target->directory = malloc(MAX_PATH_LENGTH);

				strncpy(target->directory, args[read_arguments+1], (size_t)MAX_PATH_LENGTH-1);

				directory_set = 1;
				read_arguments += 2;
			}
			else if (strcmp(args[read_arguments], "-p") == 0) {
				
				target->port = parse_int(args[read_arguments+1]);

				if (target->port == 0) {
					printf("\tInvalid port value, using default value: %i\n", DEFAULT_PORT);
					target->port = DEFAULT_PORT;
				}
				else {
					printf("\tPort number set to:\t\t\t\t\t%i\n", target->port);
				}

				port_set = 1;
				read_arguments += 2;
			}
			else if (strcmp(args[read_arguments], "-n") == 0) {
			
				target->no_threads = parse_int(args[read_arguments+1]);
				
				printf("\tNumber of threads set to:\t\t\t\t%i\n", target->no_threads);
			
				no_threads_set = 1;	
				read_arguments += 2;
			}
			else {
				printf("Unexpected parameter, expected arguments: \n\n\t%s [ -c directory | -n threads | -p port ]\n\n", args[0]);
				exit(1);
			}
		}

		server_configuration conf_from_file;
		conf_from_file.port = 0;
		conf_from_file.no_threads = 0;
		conf_from_file.directory = 0;

		read_from_file(DEFAULT_CONF, &conf_from_file);

		printf("\n");

		if(!port_set) {
			if(conf_from_file.port != 0) {
				target->port = conf_from_file.port;

				if (target->port == 0) {
					printf("\tInvalid port value, using default value: %i\n", DEFAULT_PORT);
					target->port = DEFAULT_PORT;
				}
				else {
					printf("\tPort read from configuration file:\t\t\t%i\n", target->port);
				}
			}
			else{
				target->port = DEFAULT_PORT;
				printf("\tPort was not chosen, using default value:\t\t%i\n", DEFAULT_PORT);
			}	
		}
		if(!directory_set) {
			if (conf_from_file.directory != 0) {
				target->directory = conf_from_file.directory;
				printf("\tDirectory read from configuration file:\t\t\t%s\n", target->directory);
			}
			else {
				printf("\n\n\tDirectory was not specified! Please specify it with -c option or from configuration file!\n\n");
				exit(1);
			}
		}
		if(!no_threads_set) {
			if(conf_from_file.no_threads != 0) {
				target->no_threads = conf_from_file.no_threads;
				printf("\tNumber of threads read from configuration file: \t%i\n", target->no_threads);
			}
			else {
				target->no_threads = DEFAULT_THREADS_NO;
				printf("\tNumber of threads not chosen, using default value:\t%i\n", DEFAULT_THREADS_NO);
			}
		}

		printf("\n");
	}
	return 0;
}

int client_read_and_set_arguments(int argc, char* args[], client_configuration *target) {

	if(argc < 2) {
		printf("Usage method: \n\n\t%s server_address:port [-l | -R | -e seed path | -d seed path ]\n\n", args[0]);
		exit(1);
	}

	char *fullstring = args[1];
	char *tp_index = strchr(fullstring, ':');

	if(tp_index == NULL) {
		printf("\nError: %s is not a valid address!\n\nExpected format:\n\n\t address:port\n\n", fullstring);
		exit(1);
	}

	//split the string to identify address and port by replacing : with string termination character
	*tp_index = '\0';
	target->address = fullstring;
	target->port = parse_int(tp_index+1);

	printf("\tAddress chosen:\t%s\n\tPort chosen:\t%i\n\n", target->address, target->port);
	
	
	int read_arguments = 2;

		if(argc == 3 && strcmp(args[read_arguments], "-l") == 0) {
			target->action	= LIST_ACTION;
		}
		else if(argc == 3 && strcmp(args[read_arguments], "-R") == 0) {
			target->action	= LIST_REC_ACTION;
		}
		else if(argc == 5 && strcmp(args[read_arguments], "-e") == 0) {
			target->action	= ENC_ACTION;
			target->seed	= parse_int_unsigned(args[read_arguments+1]);
			target->target	= args[read_arguments+2];
		}
		else if(argc == 5 && strcmp(args[read_arguments], "-d") == 0) {
			target->action	= DEC_ACTION;
			target->seed	= parse_int_unsigned(args[read_arguments+1]);
			target->target	= args[read_arguments+2];
		}
		else {
			printf("Usage method: \n\n\t%s server_address:port [-l | -R | -e seed path | -d seed path ]\n\n", args[0]);
			exit(1);
		}

	if (argc == 2) {
		printf("Usage method: \n\n\t%s server_address:port [-l | -R | -e seed path | -d seed path ]\n\n", args[0]);
		exit(1);
	}

	return 0;
}


/*
* Function used by the client to handle a request given by a client_configuration
*/
int client_handle_command(client_configuration *target, io_interface *server) {

	char *message = malloc(SOCK_PACKET_SIZE);

	switch(target->action) {

		case LIST_ACTION:
			write_string_to_socket(LSTF_REQ, server);
			break;
		case LIST_REC_ACTION:
			write_string_to_socket(LSTR_REQ, server);
			break;
		case ENC_ACTION:
			sprintf(message, "%s %i %s", ENCR_REQ, target->seed, target->target);
			write_string_to_socket(message, server);
			break;
		case DEC_ACTION:
			sprintf(message, "%s %i %s", DECR_REQ, target->seed, target->target);
			write_string_to_socket(message, server);
			break;
		default:
			printf("Selected action not recognized!\nApplication will now close...\n\n");
			exit(0);


	}

	int response;
	read_int_from_socket(&response, server);
	
	switch(response) {
		case FIN_MSG:
			printf("Command sent and correctly executed!\n\nApplication will now close, have a good day!\n\n");
			if(target->action == ENC_ACTION)
				log_action(target->seed, target->target);
			break;
		case MORE_MSG:
			printf("Action sent and correctly received!\nReceiving message from server...\n\n");
			//send_ack(server);
			if (LST_receive(server) < 0) {
				printf("Connection aborted from server. Message received may be incomplete...\n");
			}
			break;
		case ERR_MSG:
			printf("Action sent but something went wrong on the server-side.\nPlease check if the given command is correct and then retry...\n\n");
			break;
		case BUSY_MSG:
			printf("Action sent but not executed: the file you have chosen to encrypt is currently being used by someone else...\n\nApplication will now close, have a good day!\n\n");
			break;
		default:
			printf("The server responded with an uknown message response: %i\nServer are you ok?\n\nApplication will now close, have a good day!\n\n", response);
	}

	free(message);
	return 0;
}

void handle_requests(io_interface *target) {

	char *received = malloc(SOCK_PACKET_SIZE);

	read_string_from_socket(received, target);

	if(strcmp(LSTF_REQ, received) == 0) {
		write_int_to_socket(MORE_MSG, target);
		LSTF(".", target);
	}

	else if(strcmp(LSTR_REQ, received) == 0) {
		write_int_to_socket(MORE_MSG, target);

		//wait_for_ack(target);
		LSTR(".", target);
	}

	else{

		char *message		= received;
		char *seed		= NULL;
		char *path		= NULL;

		char *sp1 = strchr(received, ' ');
		if(sp1 != NULL) {
			*sp1 = '\0';
			seed = sp1 + 1;
		}
		char *sp2 = strchr(seed, ' ');
		if(sp2 != NULL) {
			*sp2 = '\0';
			path = sp2 + 1;
		}

		if(sp1 == NULL || sp2 == NULL)
			printf("A message was received but not recognized: \n\n\t%s\n\n", received);
		else {
			int result = 0;

			if(result != -1) {
				if(strcmp(ENCR_REQ, message) == 0)
					result = ENCR(parse_int_unsigned(seed), path);
				else if(strcmp(DECR_REQ, message) == 0)
					result = DECR(parse_int_unsigned(seed), path);
			}

			if(result == 0)
				write_int_to_socket(FIN_MSG, target);
			else if(result == -1)
				write_int_to_socket(ERR_MSG, target);
			else if(result == -2)
				write_int_to_socket(BUSY_MSG, target);

		}
	}

	free(received);
}

/*
* Function called by a listener when it's started. By default, the listener 
* will listen on a given port (default 8888) and wait to establish a new connection
*/
void *listener_startup(void *params) {

	listener_job *conf = (listener_job *)params;
	io_interface *accepted_sock = NULL;


	while(1) {

		accepted_sock = NULL;

		//Gain mutex access
		semaphore_wait(conf->rr);

		//check if the main thread is actually asking to restart instead of processing a request
		if (*(conf->restart))
			break;

		//gain mutex access to the queue
		semaphore_wait(conf->sem);
		
	
		//save the request on a local variable (so that you can release lock on the queue)
		io_interface_node *temp = dequeue(conf->queue);

		if (temp == NULL) {
			printf("Received an empty node? What?\n");
			semaphore_signal(conf->sem);
			continue;
		}
		accepted_sock = &temp->current;

		//the sock of the read node is null for some reason, just free node and continue listening
		if (accepted_sock == NULL) {
			free(temp);
			semaphore_signal(conf->sem);
			continue;
		}


		//access to the shared variable is over, release the lock before going further
		semaphore_signal(conf->sem);

		//handle the locally-saved request
		handle_requests(accepted_sock);

		//close the fd (or HANDLE) of the request
		close_socket(accepted_sock);

		//free memory allocated for the node before its reference is lost forever
		free(temp);


	}

	return NULL;
}


/*
* Function called by a new thread when client sends its request
*/
void *client_startup(void *conf_ptr) {

	client_configuration *conf = (client_configuration *)conf_ptr;

	//save server connection
	io_interface server;

	if(connect_to_server(conf->address, conf->port, &server) < 0) {
		printf("Could not connect to the given server. Please check the ip for errors and retry.\nApplication will now close...\n\n");
		exit(1);
	}

	//handle given commands
	client_handle_command(conf, &server);

	return NULL;
}


/*
* Function used to start no_listeners of listeners on a given port,
* all the created listeners will be a unique thread which will listen 
* to the given port number.
* Arguments:
* 	-no_listener:		number of listeners to start
*	-conf:			pointer to a listener_job confiuration, the number of configurations should be equal to no_listeners
*	-save_to:		pointer to the location to use to save the threads structure
* Return value:
*	On success 0 is returned, -1 otherwise (?)
*/

int start_listeners(int no_listeners, listener_job *confs, thread *save_to) {

	for(int i=0; i<no_listeners; i++) {
		if(create_thread(&save_to[i], listener_startup, (void *)confs) < 0)
			return -1;
	}

	return 0;
}

