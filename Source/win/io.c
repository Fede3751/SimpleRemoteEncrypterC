#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <FileAPI.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


#include <winsock2.h>
#include <ws2tcpip.h>

#include<locale.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define SOCK_PACKET_SIZE			5120		//5mb
#define ACK_SIGNAL					1024
#define SINGLE_THREAD_FILE_LIMIT	262144 		//256 kb
#define FINISH_MESSAGE				"\r\n.\r\n"
#define MAX_CHAR_PORT				6			//max number of bytes a port can occupy when represtend as string


typedef union {
	HANDLE id;
	SOCKET sock;
} io_interface;


typedef struct {
	HANDLE id;
} thread;

typedef struct {
	char *id;
	HANDLE fd;
	HANDLE map;
	int size;
} mapped_file;


typedef struct {
	char *source;
	char *target;
	int length;
	unsigned int seed;
} XOR_job;

/*
* Function used to create a file, implementation for the windows system.
* Arguments:
*	-path: 		path of the file to be created
* 	-target:	pointer to the io_interface structure to save the new io_interface created
* Return value:
*	On success 0 is returned, otherwise -1
*/
int create_file(LPCTSTR path) {

	HANDLE handle;
	handle = CreateFile(
		path,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (handle == INVALID_HANDLE_VALUE)
		return -1;

	return 0;
}

/*
* Function used to open a file, implementation for the windows system.
* Arguments:
* 	-path: 		path of the file to be opened
*	-target: 	pointer to the io_interface structure to save the new io_interface created
* Return value:
*	On success 0 is returned, otherwise -1
*/
int open_file(char *path, io_interface *target) {

	HANDLE handle;
	handle = CreateFile(
		(LPCTSTR)path,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (handle == INVALID_HANDLE_VALUE)
		return -1;

	target->id = handle;
	return 0;
}

/*
* Deletes the file with the given path.
* ARGUMENTS:
*	-path:		path of the file to delete
* RETURN VALUE:
*	The result of DeleteFile
*/
int delete_file(char *path) {
	return DeleteFile((LPCTSTR)path);
}

/*
* Close a given io_interface. Windows implementation.
* ARGUMENTS:
*	-target:	io_interface to close
* RETURN VALUE:
*	The result of CloseHandle
*/
int close_file(io_interface *target) {
	return CloseHandle(target->id);
}

/*
* Close a given socket. Windows implementation
* ARGUMENTS:
*	-target:	io_interface socket to close
* RETURN VALUE:
*	The result of closesocket
*/
int close_socket(io_interface *target) {
	return closesocket(target->sock) == 0 ? 0 : -1;
}

/*
* Function used to change the working directory. Windows implementation.
* ARGUMENTS:
*	-path:		string of the new path to set
* RETURN VALUE:
*	On success 0 is returned, otherwiese -1
*/
int chdir(char *path) {
	return SetCurrentDirectory((LPCTSTR)path) == 0 ? -1 : 0;
}

/*
* Function used to get the working directory. Windows implementation.
* ARGUMENTS:
*	-path:		pointer to the string to save the result to
*	-length:	max length for the path
* RETURN VALUE:
*	The value of GetCurrentDirectory
*/
int getcwd(char *path, int length) {
	return GetCurrentDirectory(length, (LPTSTR)path);
}

/*
* Function used to get size of a file. Windows implementation.
* It converts a low and high size to a long size
* ARGUMENTS:
*	-high:		high value of the size
*	-low:		low value of the size
* RETURN VALUE:
*	The converted value
*/
long get_file_size(int high, int low) {
	return (long)(((long)high) << sizeof(low) * 8) | low;
}

/*
* Set all bytes of a given location to zero. Windows implementation.
* ARGUMENTS:
*	-target:	pointer to the location of the byte to set to zero
*	-size:		number of bytes to write
* RETURN VALUE:
*	The value of GetCurrentDirectory
*/
void bzero(void *target, int size) {
	ZeroMemory(target, size);
}

/*
* Function used to map a given file to memory. Windows implementation.
* ARGUMENTS:
*	-path:		string of the file location to map to memory
*	-target:	mapped_file to save the result to
* RETURN VALUE:
*	On succes 0 is returned, on failure:
*		-2 if the requested file is currently being used by someone else
*		-1 otherwise
*/
int map_file_to_memory(char *path, mapped_file *target) {

	//oopen file and check size
	io_interface temp;

	if (open_file(path, &temp) < 0) {

		//if last error is equal to 32 it means that the file is currently being used by someone else! (Which is equal to ERROR_SHARING_VIOLATION)
		if (GetLastError() == ERROR_SHARING_VIOLATION)
			return -2;
		else
			return -1;
	}

	DWORD size_high = 0;
	DWORD size_low = GetFileSize(temp.id, &size_high);

	//save size to local variable
	long file_size = get_file_size(size_high, size_low);


	//initialize variables
	HANDLE mapped_file;
	char *view;

	//create mapepd file
	if ((mapped_file = CreateFileMapping(temp.id, NULL, PAGE_READWRITE, size_high, size_low, NULL)) == NULL) {
		close_file(&temp);
		return -1;
	}
	
	//create view of mapped file
	if ((view = (char *)MapViewOfFile(mapped_file, FILE_MAP_ALL_ACCESS, 0, 0, file_size)) == NULL) {
		CloseHandle(mapped_file);
		close_file(&temp);
		return -1;
	}


	//assign parameters
	target->id   = view;
	target->fd   = temp.id;
	target->map  = mapped_file;
	target->size = file_size;


	return 0;
}

/*
* Function used to unmap a given mapped_file from memory. Windows implementation
* ARGUMENTS:
*	-target:	the mapped_file to unmap
* RETURN VALUE:
*	On succes 0 is returned, otherwise -1
*/
int unmap_file_from_memory(mapped_file *target) {

	//unmap view
	if(UnmapViewOfFile(target->id) == 0)
		return -1;

	//close map handle
	if(CloseHandle(target->map) == 0)
		return -1;

	//close file handle
	if(CloseHandle(target->fd) == 0)
		return -1;

	return 0;
}

/*
* Function which takes an unicode strings and writes to the given char pointer the converted ascii string
*/
int to_ascii(wchar_t *input, char *out) {
	int size = WideCharToMultiByte(CP_UTF8, 0, input, (int)wcslen(input) + 1, NULL, 0, NULL, NULL);
	WideCharToMultiByte(CP_UTF8, 0, input, (int)wcslen(input) + 1, out, size, NULL, NULL);
	return 0;
}

/*
* Function which takes an ascii string and writes to the given wchar_t pointer the converted unicode string
*/
int to_uni(char *input, wchar_t *out) {
	int size = MultiByteToWideChar(CP_UTF8, 0, input, (int)strlen(input) + 1, NULL, 0);
	MultiByteToWideChar(CP_UTF8, 0, input, (int)strlen(input) + 1, out, size);
	return 0;
}


typedef struct {
	void *(*startup)(void*);
	void *args;
} thread_startup;

/*
* Wrapper function used to call a given function when a thread starts.
* The function casts the given parameter data to a thread_startup structure then call its startup procedure.
* After that, the memory which data points is freed.
* This function allows create_thread to be invisible to the user if he's using a different platform.
* NOTE: This function should not be called outside of this library, it's just a way to accomplish multi-platform multi-threading without using external libraries.
*		Making everything similar to pthread_create requirements.
*/

DWORD WINAPI _wrapper_function(void *data) {
	thread_startup *proc = (thread_startup *)data;
	proc->startup(proc->args);
	free(proc);
	return 0;
}


/*
* Function used to create a new thread. Windows implementation.
* ARGUMENTS:
*	-target:	thread structure where the created thread wants to be saved
*	-startup:	function used as startup. Just like pthread, it must be a void* function which returns a void pointer
*	-param:		void pointer to the parameters which wants to be passed to the startup function of the thread
* RETURN VALUE:
	On success 0 is returned and the new thread is created, otherwise -1.
*/
int create_thread(thread *target, void *(startup)(void*), void *param) {

	thread_startup *proc;
	if ((proc = malloc(sizeof(thread_startup))) == NULL)
		return -1;
	proc->startup = startup;
	proc->args = param;

	HANDLE thread;

	LPVOID args = (LPVOID)proc;

	if ((thread = CreateThread(NULL, 0, _wrapper_function, args, 0, NULL)) == NULL)
	{
		free(proc);
		return -1;
	}

	target->id = thread;

	return 0;
}


/*
* Function used to join an existing thread. Windows implementation
* ARGUMENTS:
*	-target: 	pointer to the thread to join
*	-res: 		pointer to the location to write thread result (NOT USED IN THIS IMPLEMENTATION, JUST GIVE NULL TO IT)
* RETURN VALUE:
*	Return value is always 0
*/
int join_thread(thread *target, void *res) {
	WaitForSingleObject(target->id, INFINITE);
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
	WSADATA wsa_data;
	int i_result;

	SOCKET listen_socket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo hints;


	if ((i_result = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0)
		return -1;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	char port_str[MAX_CHAR_PORT];
	snprintf(port_str, MAX_CHAR_PORT - 1, "%i", portno);

	if ((i_result = getaddrinfo(NULL, port_str, &hints, &result)) != 0) {
		WSACleanup();
		return -1;
	}

	listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listen_socket == INVALID_SOCKET) {
		freeaddrinfo(result);
		WSACleanup();
		return -1;
	}

	i_result = bind(listen_socket, result->ai_addr, (int)result->ai_addrlen);
	if (i_result == SOCKET_ERROR) {
		freeaddrinfo(result);
		closesocket(listen_socket);
		WSACleanup();
		return -1;
	}


	target->sock = listen_socket;


	return 0;
}


/*
* Function used to listen to a previously created sock_interface, which writes to the given target the new io_interface to communicate with.
* NOTE: this listen operates just as Unix's listen and will block the current process until a client communicates
* ARGUMENTS:
* 	-interface: 	sock_interface which wants to be listened
*	-target:		pointer to the io_interface which the accepted connection wants to be saved
* RETURN VALUE:
*	On succes 0 is returned and target is correctly set.
*	On failure:	
*			-1 is returned if there was an error while trying to listen
*			-2 is returned if there was an error while trying to accept the new client
*/
int listen_to_sock(io_interface *listen_to, io_interface *target) {

	WSADATA wsa_data;
	int i_result;
	SOCKET client_socket = INVALID_SOCKET;

	if ((i_result = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0)
		return -1;

	i_result = listen(listen_to->sock, SOMAXCONN);
	if (i_result == SOCKET_ERROR) {
		return -1;
	}

	client_socket = accept(listen_to->sock, NULL, NULL);
	if (client_socket == INVALID_SOCKET) {
		return -1;
	}
	
	target->sock = client_socket;

	return 0;
}


/*
* UNUSED FUNCTION, IT WILL JUST CALL LISTEN_TO_SOCK
*/
int listen_to_sock_non_block(io_interface *interface, io_interface *target, int timeout) {
	return listen_to_sock(interface, target);
}


/*
* Function used to connect to a remote server through sockets. Windows implementation
* ARGUMENTS:
* 	-addr: 		the address of the server to connect
*	-portno: 	port number to start the connection
*	-target: 	pointer to the io_interface where the new established connection wants to be saved
* RETURN VALUE:
*	On success 0 is returned and target is correctly set, otherwise -1
*/
int connect_to_server(char* address, int portno, io_interface *target) {

	WSADATA wsa_data;
	target->sock = INVALID_SOCKET;
	struct addrinfo *result = NULL,
		*ptr = NULL,
		hints;

	int i_result;

	if ((i_result = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0)
		return -1;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char port_str[MAX_CHAR_PORT];
	snprintf(port_str, MAX_CHAR_PORT - 1, "%i", portno);

	if ((i_result = getaddrinfo(address, port_str, &hints, &result)) != 0) {
		WSACleanup();
		return -1;
	}

	if ((target->sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == INVALID_SOCKET) {
		WSACleanup();
		return -1;
	}

	if ((i_result = connect(target->sock, result->ai_addr, (int)result->ai_addrlen)) != 0) {
		closesocket(target->sock);
		WSACleanup();
		return -1;
	}

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
	char *data = (char *)&converted;
	int left = sizeof(converted);
	int written = 0;

	do {
		written = send(target->sock, data, left, (int)NULL);
		if (written < 0) {
			return -1;
		}
		else {
			data += written;
			left -= written;
		}
	} while (left > 0);

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
	int written = 0;

	do {
		written = recv(source->sock, data, left, (int)NULL);
		if (written < 0) {
			return -1;
		}
		else {
			data += written;
			left -= written;
		}
	} while (left > 0);

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
	
	int left = strlen(source) + 1;
	int written = 0;
	char *index = source;

	do {
		written = send(target->sock, index, left, (int)NULL);
		if (written < 0) {
			return -1;
		}
		else {
			index += written;
			left -= written;
		}
	} while (left > 0);
	
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
int read_string_from_socket(char *save_to, io_interface *source) {
	
	int written = 0;
	char last_char = '*'; //just choose a char different from \0

	while (last_char != '\0') {
		written += recv(source->sock, save_to + written, SOCK_PACKET_SIZE, (int)NULL);
		last_char = *(save_to + written - 1);
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
		written = send(target->sock, index, left, (int)NULL);
		if (written < 0)
			return -1;
		else {
			index += written;
			left -= written;
		}
	} while (left > 0);

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
	char *end_buffer = malloc(strlen(string_end) + 1);
	int going = 1;

	if (buffer == NULL || end_buffer == NULL)
		return -1;

	while (going) {
		rb = recv(source->sock, buffer, SOCK_PACKET_SIZE, (int)NULL);

		if (rb < 0)
			return -1;

		int string_end_index = rb - (strlen(string_end));

		//if the string received is really small (because TCP is a scumbag and decided to split it), end_buffer must be handled correctly
		//and the current value should be shifted left until there is space for the new received string
		if (string_end_index < 0) {

			int bytes_to_shift = -string_end_index;
			char *to_shift = end_buffer + strlen(string_end) - bytes_to_shift;

			//allocate space for temporary string which will be the new end_buffer string
			char *temp;
			if ((temp = malloc(sizeof(string_end) + 1)) == NULL)
				return -1;

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
		if (strcmp(string_end, end_buffer) == 0) {

			going = 0;
			//if you can delete the end string because it was all transmitted in a single packet do it,
			//otherwise just dump what you can
			rb = rb - strlen(string_end) < 0 ? 0 : rb - strlen(string_end);

		}

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
	while (save_to != ACK_SIGNAL) {
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
	WIN32_FIND_DATA fd_file;
	HANDLE h_find = NULL;

	int bytes_written = 0;
	int size = 0;

	char s_path[SOCK_PACKET_SIZE];

	sprintf(s_path, "%s\\*.*", path);

	if ((h_find = FindFirstFile((LPCTSTR)s_path, &fd_file)) == INVALID_HANDLE_VALUE)
		return -1;

	do {

		if (strcmp((const char*)fd_file.cFileName, ".") != 0 && strcmp((const char*)fd_file.cFileName, "..") != 0) {


			sprintf(s_path, "%s\\%s", path, (char *)fd_file.cFileName);

			char to_send[SOCK_PACKET_SIZE];

			long file_size = get_file_size(fd_file.nFileSizeHigh, fd_file.nFileSizeLow);

			char *index = to_send;

			if (fd_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				index += sprintf(to_send, "%15s\t\t", "-");
			else
				index += sprintf(to_send, "%15ld\t\t", file_size);

			sprintf(index, "%s\r\n", s_path);

			print_string_to_socket(to_send, target);

		}
	} while (FindNextFile(h_find, &fd_file));


	print_string_to_socket(FINISH_MESSAGE, target);

	FindClose(h_find);

	return 0;
}

int LSTR_inner(char *path, io_interface *target,  int indentation) {

	WIN32_FIND_DATA fd_file;
	HANDLE h_find = NULL;

	int bytes_written = 0;
	int size = 0;

	char s_path[SOCK_PACKET_SIZE];

	sprintf(s_path, "%s\\*.*", path);

	if ((h_find = FindFirstFile((LPCTSTR)s_path, &fd_file)) == INVALID_HANDLE_VALUE)
		return -1;

	do {

		if (strcmp((const char *)fd_file.cFileName, ".") != 0 && strcmp((const char *)fd_file.cFileName, "..") != 0) {


			sprintf(s_path, "%s\\%s", path, (char *)fd_file.cFileName);

			char to_send[SOCK_PACKET_SIZE];

			long file_size = get_file_size(fd_file.nFileSizeHigh, fd_file.nFileSizeLow);

			char *index = to_send;

			if (fd_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				index += sprintf(to_send, "%15s", "-");
			else
				index += sprintf(to_send, "%15ld", file_size);
			
			for (int i = 0; i < indentation; i++)
				index += sprintf(index, "\t");

			sprintf(index, "%s\r\n", s_path);

			if(print_string_to_socket(to_send, target) < 0)
				return -1;

			if (fd_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				LSTR_inner(s_path, target, indentation + 1);
			
		}
	} while (FindNextFile(h_find, &fd_file));


	FindClose(h_find);

	return 0;
}

int LSTR(char *path, io_interface* target) {

	LSTR_inner(path, target, 2);

	print_string_to_socket(FINISH_MESSAGE, target);

	return 0;
}

int LST_receive(io_interface *source) {

	char *buffer;

	if ((buffer = malloc(SOCK_PACKET_SIZE)) == NULL)
		return -1;

	printf("\nSize (in bytes):\tFile name:\n\n");

	int result = print_string_from_socket(source, FINISH_MESSAGE);

	printf("\n");

	free(buffer);

	return result;
}

void *XOR_task(void *params) {
	XOR_job *job = (XOR_job *)params;

	srand(job->seed);

	for (int i = 0; i<job->length; i += 4) {
		int r = rand();

		char *rand_chr = (char *)&r;

		for (int j = 0; j<4; j++) {

			if (i + j >= job->length)
				break;

			job->target[i + j] = job->source[i + j] ^ rand_chr[j];
		}

	}

	return NULL;
}





typedef struct {
	HANDLE  id;
} semaphore;


int start_semaphore(semaphore *sem, int start_count, int max_mutex) {
	LPWSTR temp;
	if ((temp = CreateSemaphore(NULL, start_count, max_mutex, NULL)) == NULL)
		return -1;
	sem->id = temp;
	return 0;
}

int start_semaphore_ex(semaphore *sem) {
	LPWSTR temp;
	if ((temp = CreateSemaphore(NULL, 1, 1, NULL)) == NULL)
		return -1;
	sem->id = temp;
	return 0;
}

int semaphore_wait(semaphore *sem) {
	return WaitForSingleObject(sem->id, INFINITE);
}

int semaphore_signal(semaphore *sem) {
	return ReleaseSemaphore(sem->id, 1, NULL);
}

int stop_semaphore(semaphore *sem) {
	return CloseHandle(sem->id) == 0 ? -1 : 0;
}