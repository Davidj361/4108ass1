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

#define BUFSIZE 512
#define DIRECTORY "/tmp/"

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
int main(int argc, char *argv[]) {
  // TODO don't make the file outputted by folder included in argument
  // TODO use this for reading commands instead? https://stackoverflow.com/q/32474138/1766555
  if (argc < 2) {
    std::cout << "Need the input file to crack" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string decodedFile = DIRECTORY + inFile + ".decoded";
  std::string decryptedFile = DIRECTORY + inFile + ".decrypted";
  char buf[BUFSIZE];

  int fdDecodedFile = open(decodedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  std::string base64CMD = "base64 -d " + inFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2
  FILE* fBase64 = popen(base64CMD.c_str(), "w");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    return -1;
  }
  // int fdBase64 = fileno(fBase64);

  std::cout << "writing to: " + decodedFile << std::endl;
  // char* result;
  // do {
  //   result = fgets(buf, BUFSIZE, stdin);
  // } while (result != NULL);
  // while (read(fdBase64, buf, BUFSIZE) > 0) {
  //   write(fdDecodedFile, buf, BUFSIZE);
  // }
  pclose(fBase64);
  return 0;

  int fdDecryptedFile = open(decryptedFile.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  // openssl enc -d -aes256 -in /tmp/in -out /tmp/test
  std::string opensslCMD = "openssl enc -d -aes256 -in " + decodedFile + " -out " + decryptedFile;
  FILE* fOpenssl = popen(base64CMD.c_str(), "r");
  if (fOpenssl == NULL) {
    std::cout << "fOpenssl is a null pointer" << std::endl;
    return -1;
  }
  int fdOpenssl = fileno(fBase64);

  std::cout << "writing to: " + decryptedFile << std::endl;
  while (read(fdOpenssl, buf, BUFSIZE) > 0) {
    write(fdDecryptedFile, buf, BUFSIZE);
  }
  pclose(fOpenssl);
}
