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

#define BUFSIZE 2048

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cout << "Need the input file to crack" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string decodedFile = inFile + ".decoded";
  std::string decryptedFile = inFile + ".decrypted";

  // TODO delete or replace the file. as of now it doesn't do anything if the file already exists
  int fdDecodedFile = open(decodedFile.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  std::string base64CMD = "base64 -d " + inFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2
  FILE* fBase64 = popen(base64CMD.c_str(), "r");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    return -1;
  }
  int fdBase64 = fileno(fBase64);

  std::cout << "writing to: " + decodedFile << std::endl;
  char buf[BUFSIZE];
  while (read(fdBase64, buf, BUFSIZE) > 0) {
    write(fdDecodedFile, buf, BUFSIZE);
  }

  pclose(fBase64);
  return 0;

  // openssl enc -d -aes256 -in /tmp/in -out /tmp/test
  std::cout << "writing to: " + decryptedFile << std::endl;
}
