// C++ stuff
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
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

bool isFileExist(const char* path) {
  std::ifstream infile(path);
  return infile.good();
}

int decrypt(std::string inFile, std::string decryptedFile, std::string hash) {
  std::string opensslCMD = "openssl aes256 -base64 -d -in " + inFile + " -out " + decryptedFile + " -k " + hash;

  std::cout << "writing to: " + decryptedFile << std::endl;
  FILE* fOpenssl = popen(opensslCMD.c_str(), "w");
  if (fOpenssl == NULL) {
    std::cout << "fOpenssl is a null pointer" << std::endl;
    exit(1);
  }
  return pclose(fOpenssl);
}

bool isDecrypted(std::string file) {
  std::string cmd = "file " + file;
  FILE* fFile = popen(cmd.c_str(), "r");
  bool ret = false;
  if (fFile == NULL) {
    std::cout << "fFile is a null pointer" << std::endl;
    exit(1);
  }
  char* buf = 0;
  size_t bufSize;
  while ( getline(&buf, &bufSize, fFile) != -1) {
    std::string output = buf;
    if (output.find("UTF-8") != std::string::npos)
      ret = true;
  }
  pclose(fFile);
  return ret;
}

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is output
// 4th argument is the wordlist to crack with
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
  std::string decryptedFile = DIRECTORY + inFileBase + ".decrypted";
  std::string passFile = DIRECTORY + inFileBase + ".hash";
  std::string checkPass = DIRECTORY + inFileBase + ".check";
  char* buf = 0;
  size_t bufSize;
  char* passbuf = 0;
  size_t passbufSize;
  // apparently openssl will think another hash is a successful
  // decrypt but in actuality it is not
  // So a count is needed to keep all "decrypted" files where the user
  // manually checks the files to see if they look decrypted
  int count = 0;

  std::string johnCMD = john + " --wordlist=" + wordlist + " --rules=single --stdout";
  FILE* fJohn = popen(johnCMD.c_str(), "r");
  while ( getline(&passbuf, &passbufSize, fJohn) != -1) {
    std::string pass = passbuf;
    // pass[pass.length()-1] = '\0'; // breaks commands
    pass.erase(std::remove(pass.begin(), pass.end(), '\n'), pass.end());
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
    std::string hash = buf;
    hash.erase(std::remove(hash.begin(), hash.end(), '\n'), hash.end());
    hash.erase(std::remove(hash.begin(), hash.end(), ' '), hash.end());
    fclose(fPassFile);

    decrypt(inFile, decryptedFile, hash);
    if (!isDecrypted(decryptedFile))
      continue;

    std::cout << "Decrypted and found the password: " << pass << std::endl;
    break;
  }
}
