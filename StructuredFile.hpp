//---------------------------------------------------------------------------
// (c) 2014 Wolf Roediger <roediger@in.tum.de>
//---------------------------------------------------------------------------
#ifndef UTIL_STRUCTUREDFILE_H_
#define UTIL_STRUCTUREDFILE_H_
//---------------------------------------------------------------------------
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "MappedFile.hpp"
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace util {
//---------------------------------------------------------------------------
class EndOfFileException : public runtime_error {
public:
   EndOfFileException(string message) : runtime_error(message) {}
};
//---------------------------------------------------------------------------
class EndOfRecordException : public runtime_error {
public:
   EndOfRecordException(string message) : runtime_error(message) {}
};
//---------------------------------------------------------------------------
class StructuredFile {
private:
   MappedFile<char> file;
   MappedFile<char>::iterator position;
   bool endOfRecord;
   int currentRecord;

public:
   bool ignoreFirstLine;
   char fieldDelimiter;
   char recordDelimiter;

   StructuredFile(std::string filename) : file(filename) {
      ignoreFirstLine = true;
      fieldDelimiter = '\t';
      recordDelimiter = '\n';
      position = file.begin();
      endOfRecord = false;
      currentRecord = 0;
   }

   string getFilename() {
      return file.filename;
   }

   int getLineNumber() {
      return currentRecord + !ignoreFirstLine + 1;
   }

   string getNextField() {
      if (endOfRecord) {
         throw EndOfRecordException("end of record");
      }
      stringstream field;
      for (; position != file.end(); ++position) {
         char character = *position;
         if (ignoreFirstLine) {
            ignoreFirstLine = character != recordDelimiter;
            continue;
         }
         if (character == fieldDelimiter) {
            break;
         } else if (character == recordDelimiter) {
            endOfRecord = true;
            break;
         }
         field << character;
      }
      if (position == file.end()) {
         throw EndOfFileException("end of file");
      }
      ++position;
      return field.str();
   }

   void getNextRecord() {
      endOfRecord = false;
      ++currentRecord;
   }
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
