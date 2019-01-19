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

  int fdDecodedFile = open(decodedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  std::string base64CMD = "base64 -d " + inFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2
  FILE* fBase64 = popen(base64CMD.c_str(), "r");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    return -1;
  }

  std::cout << "writing to: " + decodedFile << std::endl;
  while (getline(&line, &lineSize, fBase64) != -1) {
    dprintf(fdDecodedFile, "%s", line);
    // printf("%s", line);
  }
  pclose(fBase64);
  close(fdDecodedFile);
  free(line);
  return 0;

  int fdDecryptedFile = open(decryptedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  // openssl enc -d -aes256 -in /tmp/in -out /tmp/test
  std::string opensslCMD = "openssl enc -d -aes256 -in " + decodedFile + " -out " + decryptedFile;
  FILE* fOpenssl = popen(base64CMD.c_str(), "r");
  if (fOpenssl == NULL) {
    std::cout << "fOpenssl is a null pointer" << std::endl;
    return -1;
  }
  std::cout << "writing to: " + decryptedFile << std::endl;
  pclose(fOpenssl);
}
