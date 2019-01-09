void welcome_message() {
        printf("\nProgrammazione di Sistema - Progetto 2016/17 - Windows Build\n\n");
}


int startup(char *dir) {
	if (chdir(dir) < 0) {
		printf("Error while trying to set up given directory!\nApplication will now close...\n");
		exit(1);
	}
	return 0;
}
