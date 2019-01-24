#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char* argv[]) {
  int childfd[2];
  pipe(childfd);
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork broke");
    exit(1);
  } else if (pid == 0) { // child
    char* args[] = {
      (char*) "ls",
      NULL
    };
    dup2(childfd[1], STDOUT_FILENO);
    close(childfd[0]);
    close(childfd[1]);
    execvp(args[0], args);
    perror("bad exec");
    exit(1);
  } else {
    close(childfd[1]);
    char buf2[1024];
    size_t buf2Size = 1024;
    size_t n;
    while ( n = read(childfd[0], &buf2, buf2Size), n > 0) {
      printf("LOOP START\n");
      printf("%s", buf2);
      printf("LOOP END\n");
    }
    printf("outside of loop\n");
  }
  return 0;
}
