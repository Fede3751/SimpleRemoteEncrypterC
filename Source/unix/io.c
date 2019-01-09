#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <semaphore.h>

#include <pthread.h>

#include <netdb.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/select.h>

#define SOCK_MAX_QUEUE_LENGTH		64
#define SOCK_PACKET_SIZE		5120		//5mb
#define ACK_SIGNAL			1024
#define SINGLE_THREAD_FILE_LIMIT	262144 		//256 kb
#define FINISH_MESSAGE			"\r\n.\r\n"

/*
* Union which symbolizes an input/output structre which can be read or written (i.e. a given file or a connected server)
* The following implementations is for the Unix system and only uses a file descriptor to identify the real interface 
* (differenty from Windows which can be either a SCOK or a HANDLE).
*/
typedef union {
	int id;
} io_interface;


/*
* Structure which symbolizes a thread used by thread-related functions. Implementation for the Unix system.
*/
typedef struct {
	pthread_t id;
} thread;


/*
* Structure which symbolizes a socket which can be listened.
* The following implementation is for the Unix system and use just an integer to identify the real socket.
*/
typedef struct {
	int id;
} sock_interface;


/*
* Structure which symbolizes a filed mapped in memory.
* The following implementation is for the Unix system and stores the char pointer to the mapped file and its size.
*/
typedef struct {
	char *id;
	int fd;
	long size;
} mapped_file;


/*
* Structure which defines a XOR_job for encrypting/decrypting files in parallel.
*/
typedef struct {
	char *source;
	char *target;
	int length;
	unsigned int seed;
} XOR_job;


/*
* Function used to create a file, implementation for the Unix system.
* ARGUMENTS:
*	-path: 		path of the file to be created
* 	-target:	pointer to the io_interface structure to save the new io_interface created
* RETURN VALUE:
*	On success 0 is returned and the file is created, otherwise -1 
*/
int create_file(char *path) {
	int id;	
	if((id = open(path, O_CREAT, 0777)) <  0)
		return -1;
	if(close(id) < 0)
		return -1;
	return 0;
}



/*
* Function used to open a file (readable and writable), implementation for the unix system.
* ARGUMENTS:
* 	-path: 		path of the file to be opened
*	-target: 	pointer to the io_interface structure to save the new io_interface created
* RETURN VALUE:
*	On success 0 is returned and target correctly set, otherwise -1
*/
int open_file(char *path, io_interface *target) {
	
	int id;

	//generic read/write open, not customizable in this version
	if((id = open(path, O_RDWR)) < 0)
		return -1;

	target->id = id;
	return 0;
}

int delete_file(char *path) {
	return unlink(path);
}



/*
* Function used to close a file.
* ARGUMENTS:
* 	-target: 	pointer to the io_interface structure which wants to be closed
* RETURN VALUE:
	On sueccess 0 is returned, otherwise -1
*/
int close_interface(io_interface *target) {
	
	if(close(target->id) < 0)
		return -1;

	return 0;

}

/*
* Function used to close a socket. Implementation is the same as close_interface under Unix as both socket
* and file use a fd.
* ARGUMENTS:
* 	-target: 	pointer to the io_interface structure which wants to be closed
* RETURN VALUE:
	On sueccess 0 is returned, otherwise -1
*/
int close_socket(io_interface *target) {
	
	if(close(target->id) < 0)
		return -1;

	return 0;

}

/*
* Function used to map a given file to memory. Unix implementation.
* ARGUMENTS:
*	-path:		char path of the file in the file system
*	-target:	mapped_file which the mapped file wants to be saved
* RETURN VALUE:
*	On success 0 is returned and target is correctly set, otherwise:
*		-1 if there was an error while trying to open the file
*		-2 if the lock could not be granted on the chosen file
*/
int map_file_to_memory(char *path, mapped_file *target) {

	//open file (just use personal library for comfort)
	io_interface temp;
	
	if(open_file(path, &temp) != 0) 
		return -1;

	//put non-blocking lock on file
	if (flock(temp.id, LOCK_EX | LOCK_NB) < 0) 
		return -2;

	//calculate memory to be allocated
	off_t fsize = lseek(temp.id, 0, SEEK_END);

	//allocate memory
	if((target->id = (char *)mmap(NULL, fsize, PROT_READ | PROT_WRITE, MAP_SHARED, temp.id, 0)) == MAP_FAILED) {
		flock(temp.id, LOCK_UN);
		return -1;		
	}

	//assign parameters
	target->size	= fsize;
	target->fd	= temp.id;

	return 0;
}



/*
* Function used to remove a mapped_file from memory.
* ARGUMENTS:
*	-taret:		pointer to the mappeed_file to unmap
* RETURN VALUE:
*	On succes 0 is returned, otherwise -1
*/
int unmap_file_from_memory(mapped_file *target) {
	
	//munmap allocated memory
	if(munmap(target->id, target->size) < 0)
		return -1;

	//remove lock
	if(flock(target->fd, LOCK_UN) < 0)
		return -1;

	//close file
	if (close(target->fd) < 0)
		return -1;
	
	return 0;
}



/* 
* Function used to create a new thread. Unix implementation.
* ARGUMENTS:
*	-target:	thread structure where the created thread wants to be saved
*	-startup:	function used as startup. Just like pthread, it must be a void* function which returns a void pointer
*	-param:		void pointer to the parameters which wants to be passed to the startup function of the thread
* RETURN VALUE:
	On success 0 is returned and the new thread is created, otherwise -1.
*/

int create_thread(thread *target, void *(startup)(void*), void *param) {

	if (pthread_create(&target->id, NULL, startup, param) != 0)
			return -1;
	return 0;
}



/*
* Function used to join an existing thread. Unix implementation
* ARGUMENTS:
*	-target: 	pointer to the thread to join
*	-res: 		pointer to the location to write thread result (NOT USED IN THIS IMPLEMENTATION, JUST GIVE NULL TO IT)
* RETURN VALUE:
*	On success 0 is returned and thread is succesfully joined, otherwise -1
*/
int join_thread(thread *target, void *res) {
	if(pthread_join(target->id, res) != 0)
		return -1;
	return 0;
}



/*
* Function used to host a server.
* After the sock_interface is correctly created, listen_to_sock(...) must be used in order to listen to the new sock_interface
* ARGUMENTS:
* 	-portno: 	port number which server wants to listen
*	-target: 	pointer to the sock_interface structure which wants to be saved
* RETURN VALUE:
* 	On success 0 is returned and target is correctly set, otherwise -1
*/

int host_server(int portno, io_interface *target) {

	struct sockaddr_in server_addr;
	int tcp_socket;

	if((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		return -1;

	//int options = 1;

	//setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&options, sizeof(options));
	
	target->id = tcp_socket;	

	bzero((char *) &server_addr, sizeof(server_addr));

	//configure protocol
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(portno);

	if ((bind(tcp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr))) < 0)
		return -1;

	return 0;
}



/*
* Function used to listen to a previously created sock_interface, which writes to the given target the new io_interface to communicate with.
* NOTE: this listen operates just as Unix's listen and will block the current process until a client communicates
* ARGUMENTS:
* 	-interface: 	sock_interface which wants to be listened
*	-target:	pointer to the io_interface which the accepted connection wants to be saved
* RETURN VALUE:
*	On succes 0 is returned and target is correctly set.
*	On failure:	-1 is returned if there was an error while trying to listen
*			-2 is returned if there was an error while trying to accept the new client
*/
int listen_to_sock(io_interface *interface, io_interface *target) {

	struct sockaddr_in client_addr;
	socklen_t cli_len = sizeof(client_addr);
	int new_sock_fd;

	if(listen(interface->id, SOCK_MAX_QUEUE_LENGTH) < 0)
		return -1;


	if((new_sock_fd = accept(interface->id, (struct sockaddr *)&client_addr, &cli_len)) < 0)
		return -2;

	target->id = new_sock_fd;	
	return 0;
}

/*
* Function used to listen to a previously created sock_interface for a given interval,
* it will write to the given target the new io_interface to communicate with.
* ARGUMENTS:
* 	-interface: 	sock_interface which wants to be listened
*	-target:	pointer to the io_interface which the accepted connection wants to be saved
*	-timeout:	seconds to wait for a connection
* RETURN VALUE:
*	On succes 0 is returned and target is correctly set, otherwise -1
*/
int listen_to_sock_non_block(io_interface *interface, io_interface *target, int timeout) {

	//return listen_to_sock(interface, target);

	//mark fd as listening socket
	if(listen(interface->id, SOCK_MAX_QUEUE_LENGTH) < 0)
		return -1;

	//create space for select parameters
	fd_set fds;
	struct timeval time_conf;

	//set fd to listen to
	FD_ZERO(&fds);
	FD_SET(interface->id, &fds);

	//set time value
	time_conf.tv_sec  = timeout;
	time_conf.tv_usec = 0;

	//select!
	int result = select(interface->id+1, &fds, NULL, NULL, &time_conf);

	if(result > 0) {

		struct sockaddr_in client_addr;
		socklen_t cli_len = sizeof(client_addr);

		target->id = accept(interface->id, (struct sockaddr *)&client_addr, &cli_len);
	}
	else {
		return -1;
	}

	return 0;
}


/*
* Function used to connect to a remote server through sockets.
* ARGUMENTS:
* 	-addr: 		the address of the server to connect
*	-portno: 	port number to start the connection
*	-target: 	pointer to the io_interface where the new established connection wants to be saved
* RETURN VALUE:
*	On success 0 is returned and target is correctly set.
*	On failure:	-1 is returned if the address given is invalid
*			-2 is returned if there was a problem connecting to the given address
*/
int connect_to_server(char *address, int portno, io_interface *target) {
	
	struct sockaddr_in server_addr;
	struct hostent *server;
	int sockfd;
	
	if((server = gethostbyname(address)) == NULL) 
		return -1;
	
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -2;

	//configure protocol
	bzero((char *) &server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	bcopy((char *) server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
	server_addr.sin_port = htons(portno);

	if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
		return -2; 

	target->id=sockfd;
	
	return 0;
}


/*
* Function used to write an integer to the given socket io_interface.
* ARGUMENTS:
*	-int_to_write:	integer to write to the socket
*	-target:	io_interface socket to write the int to
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int write_int_to_socket(int int_to_write, io_interface *target) {

	int32_t converted = htonl(int_to_write);
	char *data = (char*)&converted;
	int left = sizeof(converted);
	int written;
	
	do {
		written = write(target->id, data, left);
		if (written < 0) {
			return -1;
		}
		else {
			data += written;
			left -= written;
		}
	}
	while (left > 0);	

	return 0;
}


/*
* Function used to read an integer from the given socket io_interface.
* ARGUMENTS:
*	-int_ptr:	pointer to the integer where the received int wants to be written
*	-source:	io_interface socket to read the int from
* RETURN VALUE:
*	On success 0 is returned and int_ptr is correctly set, otherwise -1
*/
int read_int_from_socket(int *int_ptr, io_interface *source) {

	int32_t ret;
	char *data = (char *)&ret;
	int left = sizeof(ret);
	int written;

	do {
		written = read(source->id, data, left);
		if (written < 0) {
			return -1;
		}
		else {
			data += written;
			left -= written;
		}
	}
	while (left > 0);

	*int_ptr = ntohl(ret);

	return 0;
}


/*
* Function used to write a string to the given socket io_interface.
* ARGUMENTS:
*	-source:	pointer to the string which wants to be written to the socket
*	-target:	io_interface socket to write the string to
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int write_string_to_socket(char *source, io_interface *target) {

	int left = strlen(source)+1;
	int written;
	char *index = source;

	do {
		written = write(target->id, index, left);
		if (written < 0) {
			return -1;
		}
		else {
			index	+= written;
			left	-= written;
		}
	}
	while (left > 0);

	return 0;
}


/*
* Function used to read a string from the given socket io_interface.
* ARGUMENTS:
*	-save_to:	pointer to the location where to save the received string
*	-source:	io_interface socket to read the string from
* RETURN VALUE:
*	On success 0 is returned and save_to is correctly set, otherwise -1
*/
int read_string_from_socket(char *save_to,  io_interface *source) {

	int rb = 0; //read bytes
	char last_char = '*'; //just choose a char different from \0


	while(last_char != '\0') {
		rb += read(source->id, save_to + rb, SOCK_PACKET_SIZE);
		last_char = *(save_to + rb - 1);
	}

	return 0;
}


/*
* Function used to print a string to a given socket io_interface.
* ARGUMENTS:
*	-source:	pointer to the string which wants to be sent
*	-target:	io_interface socket which string wants to be printed to
* RETURN VALUE:
*	On success 0 is returned and save_to is correctly set, otherwise -1
*/
int print_string_to_socket(char *source, io_interface *target) {

	int left = strlen(source);
	int written;
	char *index = source;

	do {
		written = write(target->id, index, left);
		if (written < 0) {
			return -1;
		}
		else {
			index	+= written;
			left	-= written;
		}
	}
	while (left > 0);

	return 0;
}

/*
* Function used to print a string received by a given socket io_interface.
* ARGUMENTS:
*	-source:	io_interface socket which string wants to be printed from
*	-string_end:	the string which defines the end of the print loop. After this string is received the function returns 0
* RETURN VALUE:
*	On success 0 is returned and save_to is correctly set, otherwise -1
*/
int print_string_from_socket(io_interface *source, char *string_end) {
	
	int rb = 0;
	char *buffer = malloc(SOCK_PACKET_SIZE);
	char *end_buffer = malloc(strlen(string_end)+1);
	int going = 1;

	while(going) {
		rb = read(source->id, buffer, SOCK_PACKET_SIZE);

		if(rb < 0)
			return -1;
	
		int string_end_index = rb - (strlen(string_end));

		//if the string received is really small (because TCP is a scumbag and decided to split it), end_buffer must be handled correctly
		//and the current value should be shifted left until there is space for the new received string
		if(string_end_index < 0) {
			
			int bytes_to_shift = -string_end_index;
			char *to_shift = end_buffer + strlen(string_end) - bytes_to_shift;

			//allocate space for temporary string which will be the new end_buffer string
			char *temp = malloc(sizeof(string_end)+1);

			//put char termination on the buffer so that the next methods will work correctly
			//this direct assignment is safe as long that string_end is not as big as the entire buffer (why should it be?)
			buffer[rb] = '\0';

			//save the new string to temp and copy it to the end_buffer
			sprintf(temp, "%s%s", to_shift, buffer);
			strcpy(end_buffer, temp);

			//free allocated space for temp
			free(temp);


		}
		//if you received a long string, just copy the last strlen(string_end) bytes to the buffer and check them!
		else {
			memcpy(end_buffer, buffer + string_end_index, strlen(string_end));
			end_buffer[strlen(string_end)] = '\0';

		}

		//if you received string_end mark change the exit flag and remove the last bytes of the message
		//from printing (you should not print string_end if you can avoid it)
		if(strcmp(string_end, end_buffer) == 0) {

			going = 0;
			//if you can delete the end string because it was all transmitted in a single packet do it,
			//otherwise just dump what you can
			rb = rb - strlen(string_end) < 0 ? 0 : rb - strlen(string_end);

		}

		//print the string
		printf("%.*s", rb, buffer);

	}

	free(buffer);
	free(end_buffer);
	return 0;
}


/*
* Function used to wait for an ACK from the given io_interface. NOT USED
* ARGUMENTS:
*	-source:	io_interface which wants to be listened
* RETURN VALUE:
*	Always returns 0, but only after ACK is received
*/
int wait_for_ack(io_interface *source) {
	int save_to = 0;
	while(save_to != ACK_SIGNAL) {
		read_int_from_socket(&save_to, source);
	}
	return 0;
}

/*
* Functin used to send an ACK to the given io_interface. NOT USED
* ARGUMENTS:
*	-target:	io_interface which the signal wants to be sent to
* RETURN VALUE:
*	On success 0 is returned and int is sent, otherwise -1
*/
int send_ack(io_interface *target) {
	return write_int_to_socket(ACK_SIGNAL, target);
}


/*
* Function used to list all the files in the given directory to the given socket io_interface.
* ARGUMENTS:
*	-path:		char pointer with the directory which wants to be listed
*	-target:	socket io_interface which directory list wants to be sent to
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int LSTF(char *path, io_interface *target) {

	//open given directory
	DIR *d;
	struct dirent *dir;
	if((d = opendir(path)) == NULL)
		return -1;

	//allocate space for the buffer which will be sent
	char *to_send = (char *)malloc(SOCK_PACKET_SIZE);

	//while there are files to be read in the directory, read them and send the value to the target
	while((dir = readdir(d)) != NULL) {
	
		//ignore . and .. directories
		if(strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..")) {
			
			struct stat st;
			if(stat(dir->d_name, &st) != 0)
				continue;

			char *index = to_send;

			if(S_ISDIR(st.st_mode))
				index += snprintf(index, SOCK_PACKET_SIZE, "%15s", "-");
			else
				index += snprintf(index, SOCK_PACKET_SIZE, "%15jd", (intmax_t)st.st_size);

			index += snprintf(index, SOCK_PACKET_SIZE, "\t\t%s\r\n", dir->d_name);

			//send the string with write_string_to_socket function
			print_string_to_socket(to_send, target);



		}
	}


	//close directory and free allocated space
	closedir(d);
	free(to_send);

	//write finish message to the target
	print_string_to_socket(FINISH_MESSAGE, target);

	return 0;

}


/*
* List all files in the directory given by path recursively. Result is printed on the given io_interface target.
* ARGUMENTS:
*	-path:		the path of the directory which content wants to be listed
*	-target:	the io_interface which results want to be written to
* RETURN VALUE:
*	On succes 0 is returned and result is written on target, otherwise -1
*/


/*
* Inner function used by the recursion, scroll down for the real one
*/
int LSTR_inner(char *path, io_interface *target, int indentation) {

	//open given path
	DIR *d;
	struct dirent *dir;
	d = opendir(path);
	if (d == NULL)
		return -1;

	//allocate space for recursive calls on child directories
	char *s_path;
	if((s_path = (char *)malloc(SOCK_PACKET_SIZE)) == NULL)
		return -1;

	snprintf(s_path, SOCK_PACKET_SIZE, "%s", path);

	//allocate space for the buffer which will be sent
	char *to_send = (char *)malloc(SOCK_PACKET_SIZE);
	if(to_send == NULL)
		return -1;
	
	//while there are files to be read in the directory, read them and send the value to the target
	while((dir = readdir(d)) != NULL) {

		//ignore . and .. directories
		if(strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
			
			snprintf(s_path, SOCK_PACKET_SIZE, "%s/%s", path, dir->d_name);

			struct stat st;
			if(stat(s_path, &st) != 0)
				continue;


			char *index = to_send;
	
			if(!S_ISDIR(st.st_mode))
				index += snprintf(index, SOCK_PACKET_SIZE, "%15jd", (intmax_t)st.st_size);
			else
				index += snprintf(index, SOCK_PACKET_SIZE, "%15s", "-");

			for(int i=0; i<indentation; i++)
				index += snprintf(index, SOCK_PACKET_SIZE, "\t");
			
			index += snprintf(index, SOCK_PACKET_SIZE, "%s\r\n", s_path);
	
			if(print_string_to_socket(to_send, target) < 0)
				return -1;

			//if the current file is a directory, recursively call LSTR_inner on its path
			if(S_ISDIR(st.st_mode)) { 
				LSTR_inner(s_path, target, indentation + 1);
			}


		}
	
	}
	
	//close directory and free allocated space
	closedir(d);
	free(to_send);
	free(s_path);

	return 0;
}

int LSTR(char *path, io_interface *target) {

	//initialize count to enumerate files

	//call recursive function
	LSTR_inner(path, target, 2);

	//send FINISH_MESSAGE to the client
	print_string_to_socket(FINISH_MESSAGE, target);

	return 0;
}


/*
* Function used by the client to handle a LSTF or a LSTR call
* ARGUMENTS:
*	-source:	socket io_interface which wants to be listened
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int LST_receive(io_interface *source) {

	char *buffer = malloc(SOCK_PACKET_SIZE);

	printf("\nSize (in bytes):\tFile name:\n\n");
	
	print_string_from_socket(source, FINISH_MESSAGE);
	
	printf("\n");

	free(buffer);

	return 0;
}


/*
* Function used by a thread to XOR a file parallelized.
*/
void *XOR_task(void *params) {

	XOR_job *job = (XOR_job *)params;

	for(int i=0; i<job->length; i+=4) {

		//generate new random number from seed
		int r = rand_r(&job->seed);
		char *rand_chr = (char *)&r;
		
		for(int j=0; j<4; j++) {
			
			if(i + j >= job->length)
				break;
			
			//XOR single byte
			job->target[i+j] = job->source[i+j] ^ rand_chr[j];
		}
			
	}

	return NULL;
}

typedef struct {
	sem_t id;
} semaphore;


/*
* Start a semaphre with the given value
* ARGUMENTS:
*	-sem:		semaphore structure to save the semaphore to
*	-value:		starting value of the semaphore
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int start_semaphore(semaphore *sem, int value, int _not_used) {
	return sem_init(&sem->id, 0, value);
}


/*
* Start a semaphre with value 1 for mutex access to critical sections.
* ARGUMENTS:
*	-sem:		semaphore structure to save the semaphore to
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int start_semaphore_ex(semaphore *sem) {
	return sem_init(&sem->id, 1, 1);
}


/*
* Wait for a semaphore function.
* ARGUMENTS:
*	-sem:		semaphore structure to wait for
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int semaphore_wait(semaphore *sem) {
	return sem_wait(&sem->id);
}


/*
* Signal a semaphore function.
* ARGUMENTS:
*	-sem:		semaphore structure to send signal to
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int semaphore_signal(semaphore *sem) {
	return sem_post(&sem->id);
}



int stop_semaphore(semaphore *sem) {
	return sem_destroy(&sem->id);
}