#include <stdio.h>

#define ENCR_EXT		"_enc"


int XOR_file_parallel(unsigned int seed, char *path, mapped_file *source, char *out) {


	int file_size = source->size;
	int no_threads = file_size/SINGLE_THREAD_FILE_LIMIT;

	thread *jobs = (thread *)malloc(sizeof(thread) * no_threads);
	if(jobs == NULL)
		return -1;

	XOR_job *params = (XOR_job *)malloc(sizeof(XOR_job) * no_threads);
	if(params == NULL) {
		free(jobs);
		return -1;
	}
	
	char *temp = (char *)malloc(file_size);
	if(temp == NULL) {
		free(jobs);
		free(params);
		return -1;
	}

	for(int i=0; i<no_threads; i++) {

		int start_index = i * SINGLE_THREAD_FILE_LIMIT;

		params[i].source = source->id + start_index;
		params[i].target = temp + start_index;
		params[i].length = SINGLE_THREAD_FILE_LIMIT;
		params[i].seed   = seed;

		if(create_thread(&jobs[i], XOR_task, (void *)&params[i]) < 0)
			return -1;

	}

	//and last one with smaller index! (this will be done by this thread. Just call XOR_task here)

	unsigned int this_seed = seed;

	XOR_job job;
	job.source = source->id + (SINGLE_THREAD_FILE_LIMIT * no_threads);
	job.target = temp + (SINGLE_THREAD_FILE_LIMIT * no_threads);
	job.length = source->size % SINGLE_THREAD_FILE_LIMIT;
	job.seed   = this_seed;

	XOR_task((void *)&job);

	for(int i=0; i<no_threads; i++)
		join_thread(&jobs[i], NULL);

	//save new file
	FILE *new_file = fopen(out, "ab");

	if (new_file == NULL) {

		unmap_file_from_memory(source);

		free(temp);
		free(jobs);
		free(params);

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
	if(delete_file(path) < 0)
		return -1;
	
	return 0;
}


/*
* Function used to encrypt a given file and save the result of the encryption.
* ARGUMENTS:
*	-seed:		int used to generate the random numbers which will be XORed with the bytes of the file
*	-path:		char location of the file which wants to be encrypted
*	-out:		char location where the encrypted file wants to be saved
* RETURN VALUE:
*	On success 0 is returned, otherwise -1
*/
int XOR_file(unsigned int seed, char *path, char *out) {

	//map file to memory
	mapped_file source;

	int result;

	//carefully check what map_file_to_memory returns! if it is -2 it's not a real error: it means
	//that the file could not be locked and this should be treated corretly!
	if ((result = map_file_to_memory(path, &source)) < 0)
		return result;

	if(source.size > SINGLE_THREAD_FILE_LIMIT)
		return XOR_file_parallel(seed, path, &source, out);

	//allocate space for output files
	char* temp = (char *)malloc(source.size);
	
	//set random seed
	srand(seed);

	//XOR all bytes of the files
	for(int i=0; i<source.size; i+=4) {
		
		int r = rand();
		char* rand_chr = (char *)&r;

		for(int j=0; j<4; j++) {

			if(i + j >= source.size)
				break;

			temp[i+j] = source.id[i+j] ^ rand_chr[j];
		}

	}

	//save new file
	FILE *new_file = fopen(out, "ab");

	if (new_file == NULL) {

		unmap_file_from_memory(&source);
		free(temp);

		return -1;
	}

	//write the new file
	fwrite(temp, 1, source.size, new_file);

	//close new file and free allocated space
	fclose(new_file);
	unmap_file_from_memory(&source);
	free(temp);

	//delete the old file
	if(delete_file(path) < 0)
		return -1;

	return 0;
}


int ENCR(int seed, char *target) {

	char *outfile = malloc(strlen(target)+strlen(ENCR_EXT)+1);
	snprintf(outfile, strlen(target)+strlen(ENCR_EXT)+1, "%s%s", target, ENCR_EXT);

	int result = XOR_file(seed, target, outfile);

	free(outfile);
	
	return result;
}


int DECR(int seed, char *target) {

	//allocate space for input file string and copy the path to it
	char *outfile = malloc(strlen(target)+1);
	snprintf(outfile, strlen(target)+1,  "%s", target);

	//verify if file is encrypted
	char *extension = outfile+strlen(outfile)-4;
	if(strcmp(extension, ENCR_EXT) != 0)
		return -1;

	//drop the extension
	*extension = '\0';
	
	int result = XOR_file(seed, target, outfile);

	free(outfile);
	
	return result;
}