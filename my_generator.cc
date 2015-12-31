#include "my_generator.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <google/protobuf/descriptor.pb.h>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/stringprintf.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/stubs/substitute.h>

namespace my {

    // Returns a copy of |filename| with any trailing ".protodevel" or ".proto
    // suffix stripped.
    string StripProto(const string& filename) {
      const char* suffix = HasSuffixString(filename, ".protodevel")
          ? ".protodevel" : ".proto";
      return StripSuffixString(filename, suffix);
    }

    // Returns the module name expected for a given .proto filename.
    string ModuleName(const string& filename) {
      string basename = StripProto(filename);
      StripString(&basename, "-", '_');
      StripString(&basename, "/", '.');
      return basename;
    }

    string FieldNameConverter(const string& name){
    	string name_(name);
    	name_.erase(std::remove(name_.begin(), name_.end(), '_'), name_.end());
    	LowerString(&name_);
    	return name_ + "_";
    }

    // Returns the name of all containing types for descriptor,
    // in order from outermost to innermost, followed by descriptor's
    // own name.  Each name is separated by |separator|.
    template <typename DescriptorT>
    string NamePrefixedWithNestedTypes(const DescriptorT& descriptor,
                                       const string& separator) {
      string name = descriptor.name();
      for (const Descriptor* current = descriptor.containing_type();
           current != NULL; current = current->containing_type()) {
        name = current->name() + separator + name;
      }
      return name;
    }

    // Name of the class attribute where we store the
    // descriptor.Descriptor instance for the generated class.
    // Must stay consistent with the _DESCRIPTOR_KEY constant
    // in proto2/public/reflection.py.
    const char kDescriptorKey[] = "DESCRIPTOR";

    // Does the file have top-level enums?
    inline bool HasTopLevelEnums(const FileDescriptor *file) {
      return file->enum_type_count() > 0;
    }

    // Should we generate generic services for this file?
    inline bool HasGenericServices(const FileDescriptor *file) {
      return file->service_count() > 0 &&
             file->options().py_generic_services();
    }

    // Returns a literal giving the default value for a field.
    // If the field specifies no explicit default value, we'll return
    // the default default value for the field type (zero for numbers,
    // empty string for strings, empty list for repeated fields, and
    // None for non-repeated, composite fields).
    //
    // //compiler/cpp/internal/primitive_field.cc
    // //compiler/cpp/internal/enum_field.cc
    // //compiler/cpp/internal/string_field.cc
    string StringifyDefaultValue(const FieldDescriptor& field) {
      if (field.is_repeated()) {
        return "[]";
      }

      switch (field.cpp_type()) {
        case FieldDescriptor::CPPTYPE_INT32:
          return SimpleItoa(field.default_value_int32());
        case FieldDescriptor::CPPTYPE_UINT32:
          return SimpleItoa(field.default_value_uint32());
        case FieldDescriptor::CPPTYPE_INT64:
          return SimpleItoa(field.default_value_int64());
        case FieldDescriptor::CPPTYPE_UINT64:
          return SimpleItoa(field.default_value_uint64());
        case FieldDescriptor::CPPTYPE_DOUBLE: {
          double value = field.default_value_double();
          if (value == numeric_limits<double>::infinity()) {
            // pre-2.6 on Windows does not parse "inf" correctly.  However,
            // a numeric literal that is too big for a double will become infinity.
            return "1e10000";
          } else if (value == -numeric_limits<double>::infinity()) {
            // See above.
            return "-1e10000";
          } else if (value != value) {
            // infinity * 0 = nan
            return "(1e10000 * 0)";
          } else {
            return SimpleDtoa(value);
          }
        }
        case FieldDescriptor::CPPTYPE_FLOAT: {
          float value = field.default_value_float();
          if (value == numeric_limits<float>::infinity()) {
            // pre-2.6 on Windows does not parse "inf" correctly.  However,
            // a numeric literal that is too big for a double will become infinity.
            return "1e10000";
          } else if (value == -numeric_limits<float>::infinity()) {
            // See above.
            return "-1e10000";
          } else if (value != value) {
            // infinity - infinity = nan
            return "(1e10000 * 0)";
          } else {
            return SimpleFtoa(value);
          }
        }
        case FieldDescriptor::CPPTYPE_BOOL:
          return field.default_value_bool() ? "True" : "False";
        case FieldDescriptor::CPPTYPE_ENUM:
          return SimpleItoa(field.default_value_enum()->number());
        case FieldDescriptor::CPPTYPE_STRING:
    //##!PY25      return "b\"" + CEscape(field.default_value_string()) +
    //##!PY25             (field.type() != FieldDescriptor::TYPE_STRING ? "\"" :
    //##!PY25               "\".decode('utf-8')");
          return "_b(\"" + CEscape(field.default_value_string()) +  //##PY25
                 (field.type() != FieldDescriptor::TYPE_STRING ? "\")" :  //##PY25
                   "\").decode('utf-8')");  //##PY25
        case FieldDescriptor::CPPTYPE_MESSAGE:
          return "None";
      }
      // (We could add a default case above but then we wouldn't get the nice
      // compiler warning when a new type is added.)
      GOOGLE_LOG(FATAL) << "Not reached.";
      return "";
    }

    MyGenerator::MyGenerator() : file_(NULL) {
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_DOUBLE]="double";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_FLOAT]="float";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_INT64]="int64";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_UINT64]="uint64";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_INT32]="int32";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_FIXED64]="uint64";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_FIXED32]="uint32";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_BOOL]="boolean";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_STRING]="string";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_BYTES]="byte[]";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_UINT32]="uint32";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_ENUM]="enum";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_SFIXED32]="int32";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_SFIXED64]="int64";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_SINT32]="int32";
    	dataTypeNameMap[google::protobuf::FieldDescriptor::Type::TYPE_SINT64]="int64";
    }

    MyGenerator::~MyGenerator() {
    }

    void MyGenerator::PrintFileDependencies(const FileDescriptor* file) const {
    	  for (int i = 0; i < file->dependency_count(); ++i) {
    		  const FileDescriptor* depFile = file->dependency(i);
    	    string module_name = ModuleName(depFile->name());
    	    printer_->Print("import $module$\n", "module",
    	                    module_name);
    	    printer_->Indent();
    	    PrintFileDependencies(depFile);
    	    printer_->Outdent();
    	  }

    }

    bool MyGenerator::Generate(const FileDescriptor* file,
                             const string& parameter,
                             google::protobuf::compiler::GeneratorContext* context,
                             string* error) const {

      // Completely serialize all Generate() calls on this instance.  The
      // thread-safety constraints of the CodeGenerator interface aren't clear so
      // just be as conservative as possible.  It's easier to relax this later if
      // we need to, but I doubt it will be an issue.
      MutexLock lock(&mutex_);
      file_ = file;
      string module_name = ModuleName(file->name());
      string filename = module_name;
      StripString(&filename, ".", '/');
//      filename += ".my";

      FileDescriptorProto fdp;
      file_->CopyTo(&fdp);
      fdp.SerializeToString(&file_descriptor_serialized_);

//      PrintFileDependencies(file_);
      for (int i = 0; i < file_->message_type_count(); ++i) {
          const Descriptor& message_descriptor = *file_->message_type(i);

          const string outputFileName = filename + "." + message_descriptor.name() + ".my";
          scoped_ptr<io::ZeroCopyOutputStream> output(context->Open(outputFileName));
          GOOGLE_CHECK(output.get());
          io::Printer printer(output.get(), '$');
          printer_ = &printer;
        PrintDescriptor(message_descriptor);
        printer_->Print("\n");
      }

      return !printer_->failed();
    }

    string MyGenerator::GetTypeName(const google::protobuf::FieldDescriptor::Type& type) const {
    	if(dataTypeNameMap.count(type)){
    		return dataTypeNameMap.at(type);
    	}
    	return "NULL";
    }

    //Used by Generate
    void MyGenerator::PrintDescriptor(const Descriptor& message_descriptor) const {
        printer_->Print("\n");
//        printer_->Print("$descriptor_name$ = {\n",
//                        "descriptor_name",
//						message_descriptor.name());
        printer_->Print("{\n");
        printer_->Indent();
        map<string, string> m;
        PrintFieldDescriptorsInDescriptor(
            message_descriptor, /*is_extension =*/false, "fields",
            &Descriptor::field_count, &Descriptor::field);
        PrintFieldDescriptorsInDescriptor(
            message_descriptor, /*is_extension = */true, "extensions",
            &Descriptor::extension_count, &Descriptor::extension);

        printer_->Outdent();
        printer_->Print("}");
    }

    void MyGenerator::PrintFieldDescriptor(
        const FieldDescriptor& field, bool is_extension) const {
    	if(field.is_repeated()){
        	//array of primitive types or message
    		printer_->Print("[\n");
    		printer_->Indent();
    	}
        if(field.type() == FieldDescriptor::Type::TYPE_MESSAGE){
        	//message
        	PrintDescriptor(*field.message_type());
        }else {
        	//single primitive types (optional or required)
            map<string, string> m;
            m["name"] = FieldNameConverter(field.name());
            m["type"] = GetTypeName(field.type());
            // We always set message_type and enum_type to None at this point, and then
            // these fields in correctly after all referenced descriptors have been
            const char field_descriptor_decl[] =
              "$name$ : $type$";
            const char extension_descriptor_decl[] =
              "$name$ : $type$ // extension field";
            printer_->Print(m, is_extension ? extension_descriptor_decl : field_descriptor_decl);
        }
    	if(field.is_repeated()){
    		printer_->Outdent();
    		printer_->Print("\n]");
    	}
    }

    // Helper for Print{Fields,Extensions}InDescriptor().
    void MyGenerator::PrintFieldDescriptorsInDescriptor(
        const Descriptor& message_descriptor,
        bool is_extension,
        const string& list_variable_name,
        int (Descriptor::*CountFn)() const,
        const FieldDescriptor* (Descriptor::*GetterFn)(int) const) const {
      printer_->Indent();
      for (int i = 0; i < (message_descriptor.*CountFn)(); ++i) {
        PrintFieldDescriptor(*(message_descriptor.*GetterFn)(i),
                             is_extension);
        printer_->Print(",\n");
      }
      printer_->Outdent();
      printer_->Print("\n");
    }

}  // namespace my
