//---------------------------------------------------------------------------
// (c) 2014 Wolf Roediger <roediger@in.tum.de>
//---------------------------------------------------------------------------
#ifndef UTIL_MAPPEDFILE_H_
#define UTIL_MAPPEDFILE_H_
//---------------------------------------------------------------------------
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
//---------------------------------------------------------------------------
namespace util {
//---------------------------------------------------------------------------
template <typename T>
class MappedFile {
private:
   int descriptor;

public:
   std::string filename;
   size_t size;
   off_t offset;
   T* content;

   typedef T* iterator;
   typedef T value_type;
   typedef value_type *pointer;
   typedef value_type &reference;

   MappedFile(std::string filename) : filename(filename) {
      descriptor = open(filename.c_str(), O_RDONLY);
      size = fileSize()/sizeof(T);
      content = reinterpret_cast<T*>(mapFile());
   }

   ~MappedFile() {
      munmap(content, size);
      close(descriptor);
   }

   size_t fileSize() {
      struct stat statistics;
      fstat(descriptor, &statistics);
      return statistics.st_size;
   }

   char *mapFile() {
      if (size == 0) return nullptr;
      char *fileContent = reinterpret_cast<char*>(mmap(NULL, size * sizeof(T), PROT_READ, MAP_FILE|MAP_SHARED, descriptor, 0));
      if (fileContent == MAP_FAILED) {
         std::cout << "failed to open " << filename << std::endl;
         exit(EXIT_FAILURE);
      }
      return fileContent;
   }

   MappedFile::iterator begin() const {
      return content;
   }

   MappedFile::iterator end() const {
      return content + size;
   }
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
