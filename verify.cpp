//---------------------------------------------------------------------------
// (c) 2014 Wolf Roediger <roediger@in.tum.de>
//---------------------------------------------------------------------------
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <stdlib.h>
#include "MappedFile.hpp"
#include "StructuredFile.hpp"
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
class Attribute {
public:
   enum class Type {
      Integer, BigInt, Varchar, Char, Decimal, Date
   };
   string name;
   Type type;
   int length = -1;
   int precision = -1;
   bool null = true;
};
//---------------------------------------------------------------------------
enum class ParserState {
   Name, Type, TypeLength, TypePrecision, NullInfo, EndOfAttribute
};
//---------------------------------------------------------------------------
class SchemaException : public runtime_error {
public:
   SchemaException(string message) : runtime_error(message) {}
};
//---------------------------------------------------------------------------
class SchemaInputFileException : public runtime_error {
public:
   SchemaInputFileException(string message) : runtime_error(message) {}
};
//---------------------------------------------------------------------------
class SchemaReferenceFileException : public runtime_error {
public:
   SchemaReferenceFileException(string message) : runtime_error(message) {}
};
//---------------------------------------------------------------------------
class Schema {
private:
   vector<Attribute> attributes;
   int numberOfAttributes;

   void throwError(util::StructuredFile& inputFile, string message, int field = -1) {
      stringstream stream;
      stream << inputFile.getFilename() << ":" << inputFile.getLineNumber() << "\t";
      if (field > -1) {
         stream << attributes[field].name << ": ";
      }
      stream << message;
      throw SchemaException(stream.str());
   }

   pair<uint64_t, uint64_t> parseDecimal(string decimalString, int maxLength, int precision) {
      pair<uint64_t, uint64_t> decimal{0, 0};
      bool fraction = false;
      int length = 0;
      int decimalPlaces = 0;
      for (char digit : decimalString) {
         if (digit == '.') {
            fraction = true;
            continue;
         }
         if (fraction) {
            ++decimalPlaces;
            int decimalPlace = digit - '0';
            if (decimalPlaces == precision + 1) {
               if (decimalPlace > 4) {
                  ++decimal.second;
               }
               break;
            } else {
               decimal.second = decimal.second*10 + digit - '0';
            }
         } else {
            ++length;
            decimal.first = decimal.first*10 + digit - '0';
            if (false && length > maxLength) { // Do not check length
               throw SchemaException("decimal field exceeds length");
            }
         }
      }
      for (; decimalPlaces < precision; ++decimalPlaces) {
         decimal.second *= 10;
      }
      return decimal;
   }

   bool compareInteger(string input, string reference) {
      return stoi(input) == stoi(reference);
   }

   bool compareBigInt(string input, string reference) {
      return stol(input) == stol(reference);
   }

   bool compareVarchar(string input, string reference, int length) {
      if (input.length() > length) {
         throw SchemaInputFileException("varchar field exceeds length");
      }
      if (reference.length() > length) {
         throw SchemaReferenceFileException("varchar field exceeds length");
      }
      return input == reference;
   }

   bool compareChar(string input, string reference, int length) {
      if (input.length() > length) {
         throw SchemaInputFileException("character field exceeds length");
      }
      if (reference.length() > length) {
         throw SchemaReferenceFileException("character field exceeds length");
      }
      return input == reference;
   }

   double fractionToDouble(int fraction) {
      int numberOfDigits = 1;
      if (fraction != 0) {
         numberOfDigits = floor(log10(abs(fraction))) + 1;
      }
      return 1.0*fraction/pow(10, numberOfDigits);
   }

   bool compareDecimal(string input, string reference, int length, int precision, double epsilon) {
      pair<uint64_t, uint64_t> inputDecimal;
      try {
         inputDecimal = parseDecimal(input, length, precision);
      } catch (SchemaException& e) {
         throw SchemaInputFileException(e.what());
      }
      pair<uint64_t, uint64_t> referenceDecimal;
      try {
         referenceDecimal = parseDecimal(reference, length, precision);
      } catch (SchemaException& e) {
         throw SchemaReferenceFileException(e.what());
      }
      if (epsilon == 0.0) {
         return inputDecimal.first == referenceDecimal.first && inputDecimal.second == referenceDecimal.second;
      } else {
         double inputDouble = inputDecimal.first + fractionToDouble(inputDecimal.second);
         double referenceDouble = referenceDecimal.first + fractionToDouble(referenceDecimal.second);
         double delta = fabs(inputDouble - referenceDouble)/referenceDouble*100.0;
         return delta < epsilon;
      }
   }

   bool compareDate(string input, string reference) {
      return stol(input) == stol(reference);
   }

   bool compare(int attributeNumber, string input, string reference, double epsilon) {
      Attribute attribute = attributes[attributeNumber];
      if (attribute.null) {
         if (input == "null" && reference == "null") {
            return true;
         }
      } else {
         if (input == "null") {
            throw SchemaInputFileException("null not allowed");
         }
         if (reference == "null") {
            throw SchemaReferenceFileException("null not allowed");
         }
      }
      switch (attribute.type) {
         case(Attribute::Type::Integer):
         return compareInteger(input, reference);
         case(Attribute::Type::BigInt):
         return compareBigInt(input, reference);
         case(Attribute::Type::Varchar):
         return compareVarchar(input, reference, attribute.length);
         case(Attribute::Type::Char):
         return compareChar(input, reference, attribute.length);
         case(Attribute::Type::Decimal):
         return compareDecimal(input, reference, attribute.length, attribute.precision, epsilon);
         case(Attribute::Type::Date):
         return compareDate(input, reference);
      }
   }

public:
   Schema(string filename) {
      util::MappedFile<char> file(filename);
      numberOfAttributes = 0;
      unique_ptr<stringstream> stream = unique_ptr<stringstream>(new stringstream);
      ParserState state = ParserState::Name;
      Attribute attribute;
      auto position = file.begin();
      while (position != file.end()) {
         char character = *position;
         switch (state) {
            case (ParserState::Name):
            if (character == ' ') {
               attribute.name = stream->str();
               stream = unique_ptr<stringstream>(new stringstream);
               state = ParserState::Type;
               ++position;
               continue;
            }
            break;
            case (ParserState::Type):
            if (character == ' ' || character == '(' || character == '\n') {
               if (stream->str() == "integer") {
                  attribute.type = Attribute::Type::Integer;
               } else if (stream->str() == "bigint") {
                  attribute.type = Attribute::Type::BigInt;
               } else if (stream->str() == "varchar") {
                  attribute.type = Attribute::Type::Varchar;
                  attribute.length = 1;
               } else if (stream->str() == "char") {
                  attribute.type = Attribute::Type::Char;
                  attribute.length = 1;
               } else if (stream->str() == "decimal") {
                  attribute.type = Attribute::Type::Decimal;
                  attribute.length = 4;
                  attribute.precision = 2;
               } else if (stream->str() == "date") {
                  attribute.type = Attribute::Type::Date;
               } else {
                  throw SchemaException(string("unknown type ") + stream->str());
               }
               stream = unique_ptr<stringstream>(new stringstream);
               if (character == '(') {
                  state = ParserState::TypeLength;
               } else if (character == ' ') {
                  state = ParserState::NullInfo;
               } else if (character == '\n') {
                  state = ParserState::NullInfo;
                  continue;
               }
               ++position;
               continue;
            }
            break;
            case (ParserState::TypeLength):
            if (character == ' ' || character == ',' || character == '\n') {
               if (attribute.type == Attribute::Type::Integer || attribute.type == Attribute::Type::BigInt || attribute.type == Attribute::Type::Date) {
                  throw SchemaException("type cannot have a length");
               }
               attribute.length = stoi(stream->str());
               stream = unique_ptr<stringstream>(new stringstream);
               if (character == ',') {
                  state = ParserState::TypePrecision;
               } else if (character == ' ') {
                  state = ParserState::NullInfo;
               } else if (character == '\n') {
                  state = ParserState::NullInfo;
                  continue;
               }
               ++position;
               continue;
            }
            break;
            case (ParserState::TypePrecision):
            if (character == ' ' || character == '\n') {
               if (attribute.type != Attribute::Type::Decimal) {
                  throw SchemaException("type cannot have a precision");
               }
               attribute.precision = stoi(stream->str());
               stream = unique_ptr<stringstream>(new stringstream);
               state = ParserState::NullInfo;
               if (character == '\n') {
                  continue;
               }
               ++position;
               continue;
            }
            break;
            case (ParserState::NullInfo):
            if (character == '\n') {
               if (stream->str() == "not null") {
                  attribute.null = false;
               } else if (stream->str() == "null") {
                  attribute.null = true;
               } else if (stream->str() == "") {
                  attribute.null = true;
               } else {
                  throw SchemaException("invalid null info");
               }
               stream = unique_ptr<stringstream>(new stringstream);
               state = ParserState::EndOfAttribute;
               continue;
            }
            break;
            case (ParserState::EndOfAttribute):
            if (character == '\n') {
               attributes.push_back(attribute);
               attribute = Attribute();
               ++numberOfAttributes;
               state = ParserState::Name;
               ++position;
               continue;
            } else {
               throw SchemaException("missing newline at end of attribute");
            }
            break;
         }
         *stream << character;
         ++position;
      }
      // Finish last attribute
      switch (state) {
         case (ParserState::Type):
         if (stream->str() == "integer") {
            attribute.type = Attribute::Type::Integer;
         } else if (stream->str() == "bigint") {
            attribute.type = Attribute::Type::BigInt;
         } else if (stream->str() == "varchar") {
            attribute.type = Attribute::Type::Varchar;
            attribute.length = 1;
         } else if (stream->str() == "char") {
            attribute.type = Attribute::Type::Char;
            attribute.length = 1;
         } else if (stream->str() == "decimal") {
            attribute.type = Attribute::Type::Decimal;
            attribute.length = 4;
            attribute.precision = 2;
         } else if (stream->str() == "date") {
            attribute.type = Attribute::Type::Date;
         } else {
            throw SchemaException(string("unknown type ") + stream->str());
         }
         break;
         case (ParserState::TypeLength):
         if (attribute.type == Attribute::Type::Integer || attribute.type == Attribute::Type::BigInt || attribute.type == Attribute::Type::Date) {
            throw SchemaException("type cannot have a length");
         }
         attribute.length = stoi(stream->str());
         break;
         case (ParserState::TypePrecision):
         if (attribute.type != Attribute::Type::Decimal) {
            throw SchemaException("type cannot have a precision");
         }
         attribute.precision = stoi(stream->str());
         break;
         case (ParserState::NullInfo):
         if (stream->str() == "not null") {
            attribute.null = false;
         } else if (stream->str() == "null") {
            attribute.null = true;
         } else if (stream->str() == "") {
            attribute.null = true;
         } else {
            throw SchemaException("invalid null info");
         }
         default:
         break;
      }
      if (state != ParserState::Name) {
         attributes.push_back(attribute);
         ++numberOfAttributes;
      }
   }

   void compare(util::StructuredFile& inputFile, util::StructuredFile& referenceFile, double epsilon) {
      bool inputFinished = false;
      bool referenceFinished = false;
      while (true) {
         for (int field = 0; field != numberOfAttributes; ++field) {
            string input;
            try {
               input = inputFile.getNextField();
            } catch (util::EndOfFileException& e) {
               inputFinished = true;
            } catch (util::EndOfRecordException& e) {
               cout << numberOfAttributes << endl;
               throwError(inputFile, "too few fields");
            }
            string reference;
            try {
               reference = referenceFile.getNextField();
            } catch (util::EndOfFileException& e) {
               referenceFinished = true;
            } catch (util::EndOfRecordException& e) {
               throwError(referenceFile, "too few fields");
            }
            if (inputFinished && referenceFinished) {
               return;
            }
            if (inputFinished) {
               throwError(inputFile, "too few results");
            }
            if (referenceFinished) {
               throwError(inputFile, "too many results");
            }
            try {
               if (!compare(field, input, reference, epsilon)) {
                  throwError(inputFile, string("expected ") + reference + string(" got ") + input);
               }
            } catch(SchemaInputFileException& e) {
               throwError(inputFile, e.what(), field);
            } catch(SchemaReferenceFileException& e) {
               throwError(referenceFile, e.what(), field);
            }
         }
         inputFile.getNextRecord();
         referenceFile.getNextRecord();
      }
   }
};
//---------------------------------------------------------------------------
class Verifier {
private:
   string inputPath;
   string referencePath;
   string schemaPath;
   double epsilon;
   bool ignoreFirstLine;

   void parseCommandLineArguments(int argc, char *argv[]) {
      if (argc < 4 || argc > 6) {
         cerr << "Usage: " << argv[0] << " input reference schema [ignoreFirstLine] [epsilon]" << endl;
         exit(EXIT_FAILURE);
      }
      inputPath = argv[1];
      referencePath = argv[2];
      schemaPath = argv[3];
      ignoreFirstLine = true;
      if (argc > 4) {
         ignoreFirstLine = strcmp(argv[4], "true");
      }
      epsilon = 0.0;
      if (argc > 5) {
         epsilon = atof(argv[5]);
      }
      exitIfPathIsAbsent(inputPath);
      exitIfPathIsAbsent(referencePath);
      exitIfPathIsAbsent(schemaPath);
   }

   void exitIfPathIsAbsent(string path) {
      if (access(path.c_str(), F_OK) == -1) {
         cerr << path << ": no such file or directory" << endl;
         exit(EXIT_FAILURE);
      }
   }

   vector<string> getFilesInDirectory(string path, bool includeInvisible = false) {
      vector<string> result;
      DIR *directory;
      if ((directory = opendir(path.c_str())) != nullptr) {
         struct dirent *entry = readdir(directory);
         while (entry != nullptr) {
            if (entry->d_type == DT_REG && (includeInvisible || entry->d_name[0] != '.')) {
               result.push_back(entry->d_name);
            }
            entry = readdir(directory);
         }
         closedir(directory);
      } else {
         cerr << path << ": could not open directory" << endl;
         exit(EXIT_FAILURE);
      }
      return result;
   }

   string concatenatePath(string prefix, string suffix) {
      return prefix + string("/") + suffix;
   }

   void verifyResult(string filename) {
      cout << filename << endl;
      string schemaFilename = concatenatePath(schemaPath, filename);
      exitIfPathIsAbsent(schemaFilename);
      Schema schema(schemaFilename);
      string inputFilename = concatenatePath(inputPath, filename);
      exitIfPathIsAbsent(inputFilename);
      util::StructuredFile inputFile(inputFilename);
      inputFile.ignoreFirstLine = ignoreFirstLine;
      string referenceFilename = concatenatePath(referencePath, filename);
      exitIfPathIsAbsent(referenceFilename);
      util::StructuredFile referenceFile(referenceFilename);
      try {
         schema.compare(inputFile, referenceFile, epsilon);
      } catch (SchemaException& e) {
         cerr << e.what() << endl;
         cerr << "skipping file after first error" << endl;
      }
   }

public:
   Verifier(int argc, char *argv[]) {
      parseCommandLineArguments(argc, argv);
   }

   void verify() {
      auto files = getFilesInDirectory(inputPath);
      if (files.size() == 0) {
         cerr << "no input files" << endl;
      }
      for (auto file : files) {
         verifyResult(file);
      }
   }
};
//---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
   Verifier verifier(argc, argv);
   verifier.verify();
}
//---------------------------------------------------------------------------
