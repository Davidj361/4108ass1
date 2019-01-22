#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DIRECTORY "/tmp/"

int main(int argc, char* argv[]) {
  int pid = fork();

  if (pid < 0) {
    printf("error forking");
    exit(1);
  } else if (pid == 0) {
    char* args[] = {
      "./files/JohnTheRipper-unstable-jumbo/run/john",
      0
    };
    // close(STDIN_FILENO);
    // close(STDOUT_FILENO);
    // close(STDERR_FILENO);
    execvp(args[0], args);
  } else {
    int status;
    waitpid(pid, &status, 0);
  }
}
