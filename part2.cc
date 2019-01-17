#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cout << "Need the input file to crack" << std::endl;
    return 1;
  }
  std::string inFile = argv[1];
  std::string decodedFile = inFile + ".decoded";
  std::string decryptedFile = inFile + ".decrypted";

  // std::ofstream outfile;
  // outfile.open(decodedFilePath);

  int fd = open(decodedFile.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  // base64 -d practice_file.aes256. > /tmp/in2
  char *base64[] = {
    "base64",
    "-d",
    (char*) inFile.c_str(),
    0
  };
  // Maybe use posix_spawn instead?
  std::cout << "writing to: " + decodedFile << std::endl;
  dup2(fd,1);
  dup2(fd,2);
  execvp(base64[0], base64);
  close(fd);

  // openssl enc -d -aes256 -in /tmp/in2 -out /tmp/test2
  char *openssl[] = {
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
  fd = open(decryptedFile.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  std::cout << "writing to: " + decryptedFile << std::endl;
  dup2(fd,1);
  dup2(fd,2);
  execvp(openssl[0], openssl);
  close(fd);
}
