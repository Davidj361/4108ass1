// C++ stuff
#include <iostream>
#include <fstream>
#include <string>
#include <csignal> // For making breakpoints for debugging
// C stuff
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DIRECTORY "/tmp/"

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
int main(int argc, char *argv[]) {
  // TODO don't make the file outputted by folder included in argument
  if (argc < 2) {
    std::cout << "Need the input file to crack" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string decodedFile = DIRECTORY + inFile + ".decoded";
  std::string decryptedFile = DIRECTORY + inFile + ".decrypted";
  char* line = NULL;
  size_t lineSize = 0;
  int fd[2];

  int fdDecodedFile = open(decodedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  std::string base64CMD = "base64 -d " + inFile + " > " + decodedFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2

  std::cout << "writing to: " + decodedFile << std::endl;
  FILE* fBase64 = popen(base64CMD.c_str(), "w");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    exit(1);
  }
  pclose(fBase64);

  int pid = fork();

  if (pid < 0) {
    std::cout << "Fork failed" << std::endl;
    exit(1);
  } else if (pid == 0) { // the child
    int fdDecryptedFile = open(decryptedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    dup2(fd[0], 0);
    dup2(fd[1], 1);
    char* args[] = {
      "openssl",
      "enc",
      "-d",
      "-aes256",
      "-in",
      (char*) decodedFile.c_str(),
      "-out",
      (char*) decryptedFile.c_str(),
      0
    };
    // openssl enc -d -aes256 -in /tmp/in -out /tmp/test
    // std::string opensslCMD = "openssl enc -d -aes256 -in " + decodedFile + " -out " + decryptedFile;
    std::cout << "writing to: " + decryptedFile << std::endl;
    
    execvp(args[0], args);
    // fputs("lolsecret\r", fOpenssl);
  } else {
    int status;
    waitpid(pid, &status, 0); // wait for the child to finish
  }

}
