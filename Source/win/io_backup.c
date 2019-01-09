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

#define SOCK_MAX_QUEUE_LENGTH		5
#define SOCK_PACKET_SIZE			4096
#define ACK_SIGNAL					1024
#define SINGLE_THREAD_FILE_LIMIT	262144 		//256 kb
#define FINISH_MESSAGE				"\r\n.\r\n"
#define MAX_CHAR_PORT				6			//max number of bytes a port can occupy when represtend as string


typedef union {
	HANDLE id;
	SOCKET sock;
	int temp;
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


int close_socket(io_interface *target) {
	return closesocket(target->sock) == 0 ? 0 : -1;
}

int chdir(char *path) {
	return SetCurrentDirectory((LPCTSTR)path) == 0 ? -1 : 0;
}

int getcwd(char *path, int length) {
	return GetCurrentDirectory(length, (LPTSTR)path);
}


long get_file_size(int high, int low) {
	return (long)(((long)high) << sizeof(low) * 8) | low;
}

void bzero(void *target, int size) {
	ZeroMemory(target, size);
}


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
	if ((mapped_file = CreateFileMapping(temp.id, NULL, PAGE_READWRITE, size_high, size_low, NULL)) == NULL)
		return -1;
	
	//create view of mapped file
	if ((view = (char *)MapViewOfFile(mapped_file, FILE_MAP_ALL_ACCESS, 0, 0, file_size)) == NULL) {
		CloseHandle(mapped_file);
		CloseHandle(temp.id);
		return -1;
	}


	//assign parameters
	target->id   = view;
	target->fd   = temp.id;
	target->map  = mapped_file;
	target->size = file_size;


	return 0;
}

int unmap_file_from_memory(mapped_file *target) {

	//unmap view
	UnmapViewOfFile(target->id);

	//close map handle
	CloseHandle(target->map);

	//close file handle
	CloseHandle(target->fd);

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

int create_thread(thread *target, void *(startup)(void*), void *param) {

	thread_startup *proc = malloc(sizeof(thread_startup));
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

int join_thread(thread *target, void *res) {
	WaitForSingleObject(target->id, INFINITE);
	return 0;
}




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


int listen_to_sock_non_block(io_interface *interface, io_interface *target, int timeout) {
	return listen_to_sock(interface, target);
}



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

int read_string_from_socket(char *save_to, io_interface *source) {
	
	int written = 0;
	char last_char = '*'; //just choose a char different from \0

	while (last_char != '\0') {
		written += recv(source->sock, save_to + written, SOCK_PACKET_SIZE, (int)NULL);
		last_char = *(save_to + written - 1);
	}

	return 0;
}

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


int print_string_from_socket(io_interface *source, char *string_end) {

	int rb = 0;
	char *buffer = malloc(SOCK_PACKET_SIZE);
	char *end_buffer = malloc(strlen(string_end) + 1);
	int going = 1;

	while (going) {
		rb = recv(source->sock, buffer, SOCK_PACKET_SIZE, (int)NULL);

		int string_end_index = rb - (strlen(string_end));

		//if the string received is really small (because TCP is a scumbag and decided to split it), end_buffer must be handled correctly
		//and the current value should be shifted left until there is space for the new received string
		if (string_end_index < 0) {

			int bytes_to_shift = -string_end_index;
			char *to_shift = end_buffer + strlen(string_end) - bytes_to_shift;

			//allocate space for temporary string which will be the new end_buffer string
			char *temp = malloc(sizeof(string_end) + 1);

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


int wait_for_ack(io_interface *source) {
	int save_to = 0;
	while (save_to != ACK_SIGNAL) {
		read_int_from_socket(&save_to, source);
	}
	return 0;
}

int send_ack(io_interface *target) {
	return write_int_to_socket(ACK_SIGNAL, target);
}

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
	//free(s_path);

	return 0;
}

int LSTR_inner(char *path, io_interface *target,  int indentation) {

	WIN32_FIND_DATA fd_file;
	HANDLE h_find = NULL;

	int bytes_written = 0;
	int size = 0;

	char s_path[SOCK_PACKET_SIZE];

	sprintf(s_path, "%s\\*.*", path);

	if ((h_find = FindFirstFile((LPCWSTR)s_path, &fd_file)) == INVALID_HANDLE_VALUE)
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

			print_string_to_socket(to_send, target);

			if (fd_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				LSTR_inner(s_path, target, indentation + 1);
			
		}
	} while (FindNextFile(h_find, &fd_file));


	FindClose(h_find);
	//free(s_path);

	return 0;
}

int LSTR(char *path, io_interface* target) {

	//wchar_t *converted = malloc(MAX_PATH_LENGTH);
	//to_uni(path, converted);
	//int output_size = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
	//wchar_t *wstring = malloc(output_size * sizeof(wchar_t));
	//int size = MultiByteToWideChar(CP_ACP, 0, path, -1, wstring, output_size);
	LSTR_inner(path, target, 2);

	print_string_to_socket(FINISH_MESSAGE, target);

	//free(converted);
	return 0;
}

int LST_receive(io_interface *source) {

	char *buffer = malloc(SOCK_PACKET_SIZE);

	printf("\nSize (in bytes):\tFile name:\n\n");

	print_string_from_socket(source, FINISH_MESSAGE);

	printf("\n");

	free(buffer);

	return 0;
}

typedef struct {
	char *source;
	char *target;
	int length;
	unsigned int seed;
} XOR_job;

void *XOR_task(void *params) {
	XOR_job *job = (XOR_job *)params;

	//printf("Received a XOR task!\n\tSource:%p\n\tTarget:%p\n\tLength:%i\n\tSeed:%i\n\n", job->source, job->target, job->length, job->seed);

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

int XOR_file_parallel(unsigned int seed, char *path, mapped_file *source, char *out) {
	int file_size = source->size;
	int no_threads = file_size / SINGLE_THREAD_FILE_LIMIT;

	thread *jobs = (thread *)malloc(sizeof(thread) * no_threads);
	XOR_job *params = (XOR_job *)malloc(sizeof(XOR_job) * no_threads);

	char *temp = (char *)malloc(file_size);

	for (int i = 0; i<no_threads; i++) {

		int start_index = i * SINGLE_THREAD_FILE_LIMIT;

		params[i].source = source->id + start_index;
		params[i].target = temp + start_index;
		params[i].length = SINGLE_THREAD_FILE_LIMIT;
		params[i].seed = seed;

		create_thread(&jobs[i], XOR_task, (void *)&params[i]);

	}

	//and last one with smaller index! (this will be done by this thread. Just call XOR_task here)

	unsigned int this_seed = seed;

	XOR_job job;
	job.source = source->id + (SINGLE_THREAD_FILE_LIMIT * no_threads);
	job.target = temp + (SINGLE_THREAD_FILE_LIMIT * no_threads);
	job.length = source->size % SINGLE_THREAD_FILE_LIMIT;
	job.seed = this_seed;

	XOR_task((void *)&job);

	for (int i = 0; i<no_threads; i++)
		join_thread(&jobs[i], NULL);

	//save new file
	FILE *new_file = fopen(out, "ab");

	if (new_file == NULL) {

		free(temp);
		unmap_file_from_memory(source);

		return -1;
	}

	//write the new file
	fwrite(temp, 1, source->size, new_file);

	//close new file and free allocated space
	fclose(new_file);
	unmap_file_from_memory(source);
	free(temp);
	free(jobs);
	free(params);

	//delete the old file
	if (DeleteFile((LPCTSTR)path) < 0)
		return -1;

	return 0;
}

int XOR_file(unsigned int seed, char *path, char *out) {

	//map file to memory
	mapped_file source;

	int result;

	//carefully check what map_file_to_memory returns! if it is -2 it's not a real error: it means
	//that the file could not be locked and this should be treated corretly!
	if ((result = map_file_to_memory(path, &source)) < 0)
		return result;

	if (source.size > SINGLE_THREAD_FILE_LIMIT)
		return XOR_file_parallel(seed, path, &source, out);

	//allocate space for output files
	char* temp = (char *)malloc(source.size);

	//set random seed
	srand(seed);

	//XOR all bytes of the files
	for (int i = 0; i<source.size; i += 4) {
		int r = rand();
		char* rand_chr = (char *)&r;

		for (int j = 0; j<4; j++) {

			if (i + j >= source.size)
				break;

			temp[i + j] = source.id[i + j] ^ rand_chr[j];
		}

	}

	//save new file
	FILE *new_file = fopen(out, "ab");

	if (new_file == NULL) {

		free(temp);
		unmap_file_from_memory(&source);

		return -1;
	}

	//write the new file
	fwrite(temp, 1, source.size, new_file);

	//close new file and free allocated space
	fclose(new_file);
	unmap_file_from_memory(&source);
	free(temp);

	//delete the old file
	if (DeleteFile((LPCTSTR)path) < 0)
		return -1;

	return 0;

	return 0;
}





typedef struct {
	LPWSTR  id;
} semaphore;


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

int recv_and_write_result(io_interface *source, char *target) {

	int written = recv(source->sock, target, 512, 0);
	*(target + written) = '\0';

	return 0;
}

/*
* Function used to send a string to a given io_interface.
* Implementation for the Unix sistem, which uses send(SOCK)
*/
/*
int send_string_to_sock(char *source, io_interface dest) {

//initialize local variables for sending the string
int bytes_left = strlen(source);
char *cache = (char *)malloc(strlen(source));
int bytes_sent = 0;

//cache the string on a local variable, so that any changes will not touch the original one
if (strcpy_s(cache, bytes_left, source) != 0) {
return -1;
}

//send the string to the given socket
while (bytes_left > 0) {

bytes_sent = send(dest.sock, cache, SEND_PACKET_SIZE, 0);

printf("Bytes sents:%i\n", bytes_sent);

//offset the pointer to the string
cache += bytes_sent;
bytes_left -= bytes_sent;
}

//reset the pointer and then free it
cache -= bytes_sent;
free(cache);

return 0;
}*/



/*
* List all files in the directory given by path. Result is printed on the given io_interface target
* ARGUMENTS:
*	-path: the path of the directory which content wants to be listed
*	-target: the io_interface which results want to be written to
* RETURN VALUE:
*	On succes 0 is returned and result is written on target, otherwise -1
*/