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
#include <string.h>
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
  int ret;
  pid_t pid = fork();
  if (pid < 0) {
    std::cout << "fork broke in decrypt function" << std::endl;
    exit(1);
  } else if (pid == 0) { // child
    char* args[] = {
      (char*) "openssl",
      (char*) "aes256",
      (char*) "-base64",
      (char*) "-d",
      (char*) "-in",
      (char*) inFile.c_str(),
      (char*) "-out",
      (char*) decryptedFile.c_str(),
      (char*) "-k",
      (char*) hash.c_str(),
      NULL
    };
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execvp(args[0], args);
  } else {
    waitpid(pid, &ret, 0);
  }
  return ret;
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
  if (buf != NULL) {
    free(buf);
    buf = 0;
  }
  pclose(fFile);
  return ret;
}

// 1st argument is the program name
// 2nd argument is the file to crack
// 3rd argument is the john executable
// 4th argument is the wordlist to crack with
int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cout << "arguments: (inputfile) (john) (wordlist)" << std::endl;
    return -1;
  }
  std::string inFile = argv[1];
  std::string john = argv[2];
  std::string wordlist = argv[3];
  std::string inFileBase = basename((char*)inFile.c_str());
  std::string decryptedFile = DIRECTORY + inFileBase + ".decrypted";
  std::string validPassFile = DIRECTORY + inFileBase + ".validpass";
  std::string passFile = DIRECTORY + inFileBase + ".hash";
  std::string pass;
  std::string checkPass = DIRECTORY + inFileBase + ".check";
  char* buf = 0;
  size_t bufSize;
  char* passbuf = 0;
  size_t passbufSize;
  bool found = false;
  int ret = 0;
  int parentfd[2];
  int childfd[2];
  pipe(parentfd);
  pipe(childfd);

  std::string johnCMD = john + " --wordlist=" + wordlist + " --rules=single --stdout";
  // std::string johnCMD = john + " --wordlist=" + wordlist + " --stdout";

  // Was going to synchronize input with the parent and the child john the ripper
  // But I believe the command finishes instantly and it is not easy
  // to synchronize the parent and child for each exhaustive word search
  // since the child instantly finishes executing and goes out of sync with the parent anyways
  pid_t pid = fork();
  if (pid < 0) {
    std::cout << "fork broke in decrypt function" << std::endl;
    exit(1);
  } else if (pid == 0) { // child
    std::string wordlistArg = "--wordlist=" + wordlist;
    char* args[] = {
      (char*) john.c_str(),
      (char*) wordlistArg.c_str(),
      (char*) "--rules=single",
      (char*) "--stdout",
      NULL
    };
    dup2(childfd[1], STDOUT_FILENO);
    // NEED TO CLOSE PIPES OR ELSE IT HANGS
    close(childfd[0]);
    close(childfd[1]);
    execvp(args[0], args);
    perror("bad exec");
    exit(1);
  } else {
    // NEED TO CLOSE PIPES OR ELSE IT HANGS
    close(childfd[1]);
    // char buf2[1024];
    // size_t buf2Size = 1024;
    // size_t n;
    // while ( n = read(childfd[0], &buf2, buf2Size), n > 0) { // alternative reading
    FILE* test = fdopen(childfd[0], "r");
    while ( getline(&passbuf, &passbufSize, test) != -1) {
      pass = passbuf;
      if (passbuf != NULL) {
        free(passbuf);
        passbuf = NULL;
      }

      pass.erase(std::remove(pass.begin(), pass.end(), '\n'), pass.end());
      std::string p = "'" + pass + "'";
      std::string md5CMD = "echo -n " + p + " | md5sum > " + passFile;

      FILE* fMd5 = popen(md5CMD.c_str(), "r");
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
      if (buf != NULL) {
        free(buf);
        buf = 0;
      }
      hash.erase(std::remove(hash.begin(), hash.end(), '\n'), hash.end());
      hash.erase(std::remove(hash.begin(), hash.end(), ' '), hash.end());
      fclose(fPassFile);

      int status = decrypt(inFile, decryptedFile, hash);
      if (status != 0)
        continue;
      if (isDecrypted(decryptedFile)) {
        found = true;
        break;
      }
    }

    if (found) {
      std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
      std::cout << "Decrypted and found the password: " << pass << std::endl;
      std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
      ret = 0;
      std::ofstream out;
      out.open(validPassFile.c_str(), std::ofstream::out);
      out << pass << std::endl;
      out.close();
    } else {
      std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
      std::cout << "Couldn't find the password" << std::endl;
      std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
      ret = 1;
    }
    return ret;
  }
}
