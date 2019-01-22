// C++ stuff
#include <iostream>
#include <fstream>
#include <string>
// C stuff
#include <fcntl.h>
#include <libgen.h>
#include <signal.h> // For making breakpoints for debugging
#include <stdlib.h>
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
  if (argc < 4) {
    std::cout << "arguments: (inputfile) (john) (wordlist)" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string john = argv[2];
  std::string wordlist = argv[3];
  std::string inFileBase = basename((char*)inFile.c_str());
  std::string decodedFile = DIRECTORY + inFileBase + ".decoded";
  std::string decryptedFile = DIRECTORY + inFileBase + ".decrypted";
  std::string passFile = DIRECTORY + inFileBase + ".hash";
  char* buf = 0;
  size_t bufSize;
  char* passbuf = 0;
  size_t passbufSize;

  std::string base64CMD = "base64 -d " + inFile + " > " + decodedFile; // e.g. base64 -d practice_file.aes256. > /tmp/in2
  std::string johnCMD = john + " --wordlist=" + wordlist + " --rules=single --stdout";

  std::cout << "writing to: " + decodedFile << std::endl;
  FILE* fBase64 = popen(base64CMD.c_str(), "w");
  if (fBase64 == NULL) {
    std::cout << "fBase64 is a null pointer" << std::endl;
    exit(1);
  }
  pclose(fBase64);

  FILE* fJohn = popen(johnCMD.c_str(), "r");
  while ( getline(&passbuf, &passbufSize, fJohn) != -1) {
    std::string pass = passbuf;
    pass[pass.length()-1] = '\0';
    std::cout << "trying password: " << pass << std::endl;
    std::string md5CMD = "echo -n " + pass + " | md5sum > " + passFile;

    std::cout << "writing to: " + passFile << std::endl;
    FILE* fMd5 = popen(md5CMD.c_str(), "w");
    if (fMd5 == NULL) {
      std::cout << "fMd5 is a null pointer" << std::endl;
      exit(1);
    }
    pclose(fMd5);

    FILE* fPassFile = fopen(passFile.c_str(), "r");
    if (getdelim(&buf, &bufSize, ' ', fPassFile) == -1) {
      std::cout << "bad fPassFile read" << std::endl;
      exit(1);
    }
    // openssl enc -d -aes256 -in /tmp/practice_file.aes256.nohash.txt.decoded -out /tmp/1 -pass pass:lolsecret
    std::string opensslCMD = "openssl enc -d -aes256 -in " + decodedFile + " -out " + decryptedFile + " -pass pass:" + buf;
    free(buf);
    fclose(fPassFile);

    std::cout << "writing to: " + decryptedFile << std::endl;
    FILE* fOpenssl = popen(opensslCMD.c_str(), "w");
    if (fBase64 == NULL) {
      std::cout << "fOpenssl is a null pointer" << std::endl;
      exit(1);
    }
    pclose(fOpenssl);
  }
  free(passbuf);
  pclose(fJohn);
}
