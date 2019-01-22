#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DIRECTORY "/tmp/"

int main(int argc, char* argv[]) {
  char* buf = 0;
  size_t bufSize;
  FILE* fJohn = popen("./files/JohnTheRipper-unstable-jumbo/run/john --stdout", "r");
  while ( getline(&buf, &bufSize, fJohn) != -1) {
    printf("%s", buf);
  }
  free(buf);
  pclose(fJohn);
}
