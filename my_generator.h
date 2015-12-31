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
      void PrintImports() const;
      void PrintFileDescriptor() const;
      void PrintTopLevelEnums() const;
      void PrintAllNestedEnumsInFile() const;
      void PrintNestedEnums(const google::protobuf::Descriptor& descriptor) const;
      void PrintEnum(const EnumDescriptor& enum_descriptor) const;

      void PrintTopLevelExtensions() const;

      void PrintFieldDescriptor(
          const FieldDescriptor& field, bool is_extension) const;
      void PrintFieldDescriptorsInDescriptor(
          const Descriptor& message_descriptor,
          bool is_extension,
          const string& list_variable_name,
          int (Descriptor::*CountFn)() const,
          const FieldDescriptor* (Descriptor::*GetterFn)(int) const) const;
      void PrintFieldsInDescriptor(const Descriptor& message_descriptor) const;
      void PrintExtensionsInDescriptor(const Descriptor& message_descriptor) const;
      void PrintMessageDescriptors() const;
      void PrintDescriptor(const Descriptor& message_descriptor) const;
      void PrintNestedDescriptors(const Descriptor& containing_descriptor) const;

      void PrintMessages() const;
      void PrintMessage(const Descriptor& message_descriptor, const string& prefix,
                        vector<string>* to_register) const;
      void PrintNestedMessages(const Descriptor& containing_descriptor,
                               const string& prefix,
                               vector<string>* to_register) const;

      void FixForeignFieldsInDescriptors() const;
      void FixForeignFieldsInDescriptor(
          const Descriptor& descriptor,
          const Descriptor* containing_descriptor) const;
      void FixForeignFieldsInField(const Descriptor* containing_type,
                                   const FieldDescriptor& field,
                                   const string& dict_name) const;
      void AddMessageToFileDescriptor(const Descriptor& descriptor) const;
      void AddEnumToFileDescriptor(const EnumDescriptor& descriptor) const;
      void AddExtensionToFileDescriptor(const FieldDescriptor& descriptor) const;
      string FieldReferencingExpression(const Descriptor* containing_type,
                                        const FieldDescriptor& field,
                                        const string& dict_name) const;
      template <typename DescriptorT>
      void FixContainingTypeInDescriptor(
          const DescriptorT& descriptor,
          const Descriptor* containing_descriptor) const;

      void FixForeignFieldsInExtensions() const;
      void FixForeignFieldsInExtension(
          const FieldDescriptor& extension_field) const;
      void FixForeignFieldsInNestedExtensions(const Descriptor& descriptor) const;

      void PrintServices() const;
      void PrintServiceDescriptor(const ServiceDescriptor& descriptor) const;
      void PrintServiceClass(const ServiceDescriptor& descriptor) const;
      void PrintServiceStub(const ServiceDescriptor& descriptor) const;

      void PrintEnumValueDescriptor(const EnumValueDescriptor& descriptor) const;
      string OptionsValue(const string& class_name,
                          const string& serialized_options) const;
      bool GeneratingDescriptorProto() const;

      template <typename DescriptorT>
      string ModuleLevelDescriptorName(const DescriptorT& descriptor) const;
      string ModuleLevelMessageName(const Descriptor& descriptor) const;
      string ModuleLevelServiceDescriptorName(
          const ServiceDescriptor& descriptor) const;

      template <typename DescriptorT, typename DescriptorProtoT>
      void PrintSerializedPbInterval(
          const DescriptorT& descriptor, DescriptorProtoT& proto) const;

      void FixAllDescriptorOptions() const;
      void FixOptionsForField(const FieldDescriptor& field) const;
      void FixOptionsForEnum(const EnumDescriptor& descriptor) const;
      void FixOptionsForMessage(const Descriptor& descriptor) const;

      void PrintFileDependencies(const FileDescriptor* file) const;

      // Very coarse-grained lock to ensure that Generate() is reentrant.
      // Guards file_, printer_ and file_descriptor_serialized_.
      mutable Mutex mutex_;
      mutable const FileDescriptor* file_;  // Set in Generate().  Under mutex_.
      mutable string file_descriptor_serialized_;
      mutable io::Printer* printer_;  // Set in Generate().  Under mutex_.
      map<string, Descriptor*> messageTypeMap_;
      map<google::protobuf::FieldDescriptor::Type, string> dataTypeNameMap;

      GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(MyGenerator);
    };


}  // namespace my
#endif  // MY_GENERATOR_H__
