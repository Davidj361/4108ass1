// C++ stuff
#include <iostream>
#include <fstream>
#include <string>
// C stuff
#include <fcntl.h>
#include <libgen.h>
#include <signal.h> // For making breakpoints for debugging
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DIRECTORY "/tmp/"

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
int main(int argc, char *argv[]) {
  // TODO: check for substring "bad decrypt" from openssl if it's a bad pass
  if (argc < 2) {
    std::cout << "Need the input file to crack" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string inFileBase = basename((char*)inFile.c_str());
  std::string decodedFile = DIRECTORY + inFileBase + ".decoded";
  std::string decryptedFile = DIRECTORY + inFileBase + ".decrypted";
  std::string passFile = DIRECTORY + inFileBase + ".hash";
  std::string pass = "lolsecret";
  int fd[2];
  if (pipe(fd) == -1) {
    std::cout << "Bad pipe creation" << std::endl;
    exit(1);
  }

  int fdDecodedFile = open(decodedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  std::string base64CMD = "base64 -d " + inFile + " > " + decodedFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2
  std::string md5CMD = "echo -n " + pass + " | md5sum > " + passFile;
  // openssl enc -d -aes256 -in /tmp/practice_file.aes256.nohash.txt.decoded -out /tmp/1 -pass pass:lolsecret
  std::string opensslCMD = "openssl enc -d -aes256 -in " + decodedFile + " -out " + decryptedFile + " -pass pass:" + pass;

  std::cout << "writing to: " + decodedFile << std::endl;
  FILE* fBase64 = popen(base64CMD.c_str(), "w");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    exit(1);
  }
  pclose(fBase64);

  std::cout << "writing to: " + passFile << std::endl;
  FILE* fMd5 = popen(md5CMD.c_str(), "w");
  if (fMd5 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    exit(1);
  }
  pclose(fMd5);

  std::cout << "writing to: " + decryptedFile << std::endl;
  FILE* fOpenssl = popen(opensslCMD.c_str(), "w");
  if (fBase64 == NULL) {
    std::cout << "fOpenssl is a null pointer" << std::endl;
    exit(1);
  }
  pclose(fOpenssl);

  /*
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
    
    execvp(args[0], args);
    // fputs("lolsecret\r", fOpenssl);
  } else {
    int status;
    std::cout << "writing to: " + decryptedFile << std::endl;
    waitpid(pid, &status, 0); // wait for the child to finish
  }

}
