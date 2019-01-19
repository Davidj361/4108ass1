#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DIRECTORY "/tmp/"

int main(int argc, char* argv[]) {
  char* line = NULL;
  size_t lineSize = 0;
  ssize_t nread;
  int out = open("/tmp/1", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  // if (-1 == dup2(out, fileno(stdout))) { perror("cannot redirect stdout"); return -1; }

  FILE* fCmd = popen("/bin/ls ~", "r");
  if (fCmd == NULL) {
    printf("\nCouldn't popen, NULL pointer.\n");
    exit(1);
  }

  while( (nread = getline(&line, &lineSize, fCmd)) != -1) {
    // printf("%s", line);
    dprintf(out, "%s", line);
  }
  free(line);
  pclose(fCmd);
}
