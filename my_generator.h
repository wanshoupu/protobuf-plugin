#ifndef MY_GENERATOR_H__
#define MY_GENERATOR_H__

#include <string>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>

using namespace std;
using namespace google::protobuf;
namespace my {
    // google::protobuf::compiler::CodeGenerator implementation for generated protocol buffer classes.
    // If you create your own protocol compiler binary and you want it to support
    // output, you can do so by registering an instance of this
    // google::protobuf::compiler::CodeGenerator with the CommandLineInterface in your main() function.
    class LIBPROTOC_EXPORT MyGenerator : public google::protobuf::compiler::CodeGenerator {
     public:
      MyGenerator();
      virtual ~MyGenerator();

      // google::protobuf::compiler::CodeGenerator methods.
      virtual bool Generate(const google::protobuf::FileDescriptor* file,
                            const string& parameter,
                            google::protobuf::compiler::GeneratorContext* generator_context,
                            string* error) const;


     private:
      string GetTypeName(const google::protobuf::FieldDescriptor::Type& type) const;
      void PrintFieldDescriptor(
          const FieldDescriptor& field, bool is_extension) const;
      void PrintFieldDescriptorsInDescriptor(
          const Descriptor& message_descriptor,
          bool is_extension,
          const string& list_variable_name,
          int (Descriptor::*CountFn)() const,
          const FieldDescriptor* (Descriptor::*GetterFn)(int) const) const;
      void PrintDescriptor(const Descriptor& message_descriptor) const;

      //Test file dependencies
      void PrintFileDependencies(const FileDescriptor* file) const;

      // Very coarse-grained lock to ensure that Generate() is reentrant.
      // Guards file_, printer_ and file_descriptor_serialized_.
      mutable Mutex mutex_;
      mutable const FileDescriptor* file_;  // Set in Generate().  Under mutex_.
      mutable string file_descriptor_serialized_;
      mutable io::Printer* printer_;  // Set in Generate().  Under mutex_.
      map<google::protobuf::FieldDescriptor::Type, string> dataTypeNameMap;

      GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(MyGenerator);
    };


}  // namespace my
#endif  // MY_GENERATOR_H__
