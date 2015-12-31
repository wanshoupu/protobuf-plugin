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

//       FixForeignFieldsInDescriptors();
//       PrintMessages();
       // We have to fix up the extensions after the message classes themselves,
       // since they need to call static RegisterExtension() methods on these
       // classes.
//       FixForeignFieldsInExtensions();
       // Descriptor options may have custom extensions. These custom options
       // can only be successfully parsed after we register corresponding
       // extensions. Therefore we parse all options again here to recognize
       // custom options that may be unknown when we define the descriptors.
//       FixAllDescriptorOptions();
//       if (HasGenericServices(file)) {
//         PrintServices();
//       }

      return !printer_->failed();
    }

    string MyGenerator::GetTypeName(const google::protobuf::FieldDescriptor::Type& type) const {
    	if(dataTypeNameMap.count(type)){
    		return dataTypeNameMap.at(type);
    	}
    	return "NULL";
    }

    // Prints descriptors and module-level constants for all top-level
    // enums defined in |file|.
    void MyGenerator::PrintTopLevelEnums() const {
      vector<pair<string, int> > top_level_enum_values;
      for (int i = 0; i < file_->enum_type_count(); ++i) {
        const EnumDescriptor& enum_descriptor = *file_->enum_type(i);
        PrintEnum(enum_descriptor);
        printer_->Print("$name$ = "
                        "enum_type_wrapper.EnumTypeWrapper($descriptor_name$)",
                        "name", enum_descriptor.name(),
                        "descriptor_name",
                        ModuleLevelDescriptorName(enum_descriptor));
        printer_->Print("\n");

        for (int j = 0; j < enum_descriptor.value_count(); ++j) {
          const EnumValueDescriptor& value_descriptor = *enum_descriptor.value(j);
          top_level_enum_values.push_back(
              make_pair(value_descriptor.name(), value_descriptor.number()));
        }
      }

      for (int i = 0; i < top_level_enum_values.size(); ++i) {
        printer_->Print("$name$ = $value$\n",
                        "name", top_level_enum_values[i].first,
                        "value", SimpleItoa(top_level_enum_values[i].second));
      }
      printer_->Print("\n");
    }

    // Prints a statement assigning the appropriate module-level
    // enum name to a EnumDescriptor object equivalent to
    // enum_descriptor.
    void MyGenerator::PrintEnum(const EnumDescriptor& enum_descriptor) const {
      map<string, string> m;
      string module_level_descriptor_name =
          ModuleLevelDescriptorName(enum_descriptor);
      m["descriptor_name"] = module_level_descriptor_name;
      m["name"] = enum_descriptor.name();
      m["full_name"] = enum_descriptor.full_name();
      m["file"] = kDescriptorKey;
      const char enum_descriptor_template[] =
          "$descriptor_name$ = _descriptor.EnumDescriptor(\n"
          "  name='$name$',\n"
          "  full_name='$full_name$',\n"
          "  filename=None,\n"
          "  file=$file$,\n"
          "  values=[\n";
      string options_string;
      enum_descriptor.options().SerializeToString(&options_string);
      printer_->Print(m, enum_descriptor_template);
      printer_->Indent();
      printer_->Indent();
      for (int i = 0; i < enum_descriptor.value_count(); ++i) {
        PrintEnumValueDescriptor(*enum_descriptor.value(i));
        printer_->Print(",\n");
      }
      printer_->Outdent();
      printer_->Print("],\n");
      printer_->Print("containing_type=None,\n");
      printer_->Print("options=$options_value$,\n",
                      "options_value",
                      OptionsValue("EnumOptions", options_string));
      EnumDescriptorProto edp;
      PrintSerializedPbInterval(enum_descriptor, edp);
      printer_->Outdent();
      printer_->Print(")\n");
      printer_->Print("_sym_db.RegisterEnumDescriptor($name$)\n", "name",
                      module_level_descriptor_name);
      printer_->Print("\n");
    }

    // Recursively prints enums in nested types within descriptor, then
    // prints enums contained at the top level in descriptor.
    void MyGenerator::PrintNestedEnums(const Descriptor& descriptor) const {
      for (int i = 0; i < descriptor.nested_type_count(); ++i) {
        PrintNestedEnums(*descriptor.nested_type(i));
      }

      for (int i = 0; i < descriptor.enum_type_count(); ++i) {
        PrintEnum(*descriptor.enum_type(i));
      }
    }

    void MyGenerator::PrintServices() const {
      for (int i = 0; i < file_->service_count(); ++i) {
        PrintServiceDescriptor(*file_->service(i));
        PrintServiceClass(*file_->service(i));
        PrintServiceStub(*file_->service(i));
        printer_->Print("\n");
      }
    }

    void MyGenerator::PrintServiceDescriptor(
        const ServiceDescriptor& descriptor) const {
      printer_->Print("\n");
      string service_name = ModuleLevelServiceDescriptorName(descriptor);
      string options_string;
      descriptor.options().SerializeToString(&options_string);

      printer_->Print(
          "$service_name$ = _descriptor.ServiceDescriptor(\n",
          "service_name", service_name);
      printer_->Indent();
      map<string, string> m;
      m["name"] = descriptor.name();
      m["full_name"] = descriptor.full_name();
      m["file"] = kDescriptorKey;
      m["index"] = SimpleItoa(descriptor.index());
      m["options_value"] = OptionsValue("ServiceOptions", options_string);
      const char required_function_arguments[] =
          "name='$name$',\n"
          "full_name='$full_name$',\n"
          "file=$file$,\n"
          "index=$index$,\n"
          "options=$options_value$,\n";
      printer_->Print(m, required_function_arguments);

      ServiceDescriptorProto sdp;
      PrintSerializedPbInterval(descriptor, sdp);

      printer_->Print("methods=[\n");
      for (int i = 0; i < descriptor.method_count(); ++i) {
        const MethodDescriptor* method = descriptor.method(i);
        method->options().SerializeToString(&options_string);

        m.clear();
        m["name"] = method->name();
        m["full_name"] = method->full_name();
        m["index"] = SimpleItoa(method->index());
        m["serialized_options"] = CEscape(options_string);
        m["input_type"] = ModuleLevelDescriptorName(*(method->input_type()));
        m["output_type"] = ModuleLevelDescriptorName(*(method->output_type()));
        m["options_value"] = OptionsValue("MethodOptions", options_string);
        printer_->Print("_descriptor.MethodDescriptor(\n");
        printer_->Indent();
        printer_->Print(
            m,
            "name='$name$',\n"
            "full_name='$full_name$',\n"
            "index=$index$,\n"
            "containing_service=None,\n"
            "input_type=$input_type$,\n"
            "output_type=$output_type$,\n"
            "options=$options_value$,\n");
        printer_->Outdent();
        printer_->Print("),\n");
      }

      printer_->Outdent();
      printer_->Print("])\n\n");
    }

    void MyGenerator::PrintServiceClass(const ServiceDescriptor& descriptor) const {
      // Print the service.
      printer_->Print("$class_name$ = service_reflection.GeneratedServiceType("
                      "'$class_name$', (_service.Service,), dict(\n",
                      "class_name", descriptor.name());
      printer_->Indent();
      printer_->Print(
          "$descriptor_key$ = $descriptor_name$,\n",
          "descriptor_key", kDescriptorKey,
          "descriptor_name", ModuleLevelServiceDescriptorName(descriptor));
      printer_->Print(
          "__module__ = '$module_name$'\n",
          "module_name", ModuleName(file_->name()));
      printer_->Print("))\n\n");
      printer_->Outdent();
    }

    void MyGenerator::PrintServiceStub(const ServiceDescriptor& descriptor) const {
      // Print the service stub.
      printer_->Print("$class_name$_Stub = "
                      "service_reflection.GeneratedServiceStubType("
                      "'$class_name$_Stub', ($class_name$,), dict(\n",
                      "class_name", descriptor.name());
      printer_->Indent();
      printer_->Print(
          "$descriptor_key$ = $descriptor_name$,\n",
          "descriptor_key", kDescriptorKey,
          "descriptor_name", ModuleLevelServiceDescriptorName(descriptor));
      printer_->Print(
          "__module__ = '$module_name$'\n",
          "module_name", ModuleName(file_->name()));
      printer_->Print("))\n\n");
      printer_->Outdent();
    }

    void MyGenerator::PrintDescriptor(const Descriptor& message_descriptor) const {
        PrintNestedDescriptors(message_descriptor);
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

    // Prints Descriptor objects for all nested types contained in
    // message_descriptor.
    //
    // Mutually recursive with PrintDescriptor().
    void MyGenerator::PrintNestedDescriptors(
        const Descriptor& containing_descriptor) const {
      for (int i = 0; i < containing_descriptor.nested_type_count(); ++i) {
        PrintDescriptor(*containing_descriptor.nested_type(i));
      }
    }

    // Prints all messages in |file|.
    void MyGenerator::PrintMessages() const {
      for (int i = 0; i < file_->message_type_count(); ++i) {
        vector<string> to_register;
        PrintMessage(*file_->message_type(i), "", &to_register);
        for (int j = 0; j < to_register.size(); ++j) {
          printer_->Print("_sym_db.RegisterMessage($name$)\n", "name",
                          to_register[j]);
        }
        printer_->Print("\n");
      }
    }

    // Prints a class for the given message descriptor.  We defer to the
    // metaclass to do almost all of the work of actually creating a useful class.
    // The purpose of this function and its many helper functions above is merely
    // to output a version of the descriptors, which the metaclass in
    // reflection.py will use to construct the meat of the class itself.
    //
    // Mutually recursive with PrintNestedMessages().
    // Collect nested message names to_register for the symbol_database.
    void MyGenerator::PrintMessage(const Descriptor& message_descriptor,
                                 const string& prefix,
                                 vector<string>* to_register) const {
      string qualified_name(prefix + message_descriptor.name());
      to_register->push_back(qualified_name);
      printer_->Print(
          "$name$ = _reflection.GeneratedProtocolMessageType('$name$', "
          "(_message.Message,), dict(\n",
          "name", message_descriptor.name());
      printer_->Indent();

      PrintNestedMessages(message_descriptor, qualified_name + ".", to_register);
      map<string, string> m;
      m["descriptor_key"] = kDescriptorKey;
      m["descriptor_name"] = ModuleLevelDescriptorName(message_descriptor);
      printer_->Print(m, "$descriptor_key$ = $descriptor_name$,\n");
      printer_->Print("__module__ = '$module_name$'\n",
                      "module_name", ModuleName(file_->name()));
      printer_->Print("# @@protoc_insertion_point(class_scope:$full_name$)\n",
                      "full_name", message_descriptor.full_name());
      printer_->Print("))\n");
      printer_->Outdent();
    }

    // Prints all nested messages within |containing_descriptor|.
    // Mutually recursive with PrintMessage().
    void MyGenerator::PrintNestedMessages(const Descriptor& containing_descriptor,
                                        const string& prefix,
                                        vector<string>* to_register) const {
      for (int i = 0; i < containing_descriptor.nested_type_count(); ++i) {
        printer_->Print("\n");
        PrintMessage(*containing_descriptor.nested_type(i), prefix, to_register);
        printer_->Print(",\n");
      }
    }

    // Recursively fixes foreign fields in all nested types in |descriptor|, then
    // sets the message_type and enum_type of all message and enum fields to point
    // to their respective descriptors.
    // Args:
    //   descriptor: descriptor to print fields for.
    //   containing_descriptor: if descriptor is a nested type, this is its
    //       containing type, or NULL if this is a root/top-level type.
    void MyGenerator::FixForeignFieldsInDescriptor(
        const Descriptor& descriptor,
        const Descriptor* containing_descriptor) const {
      for (int i = 0; i < descriptor.nested_type_count(); ++i) {
        FixForeignFieldsInDescriptor(*descriptor.nested_type(i), &descriptor);
      }

      for (int i = 0; i < descriptor.field_count(); ++i) {
        const FieldDescriptor& field_descriptor = *descriptor.field(i);
        FixForeignFieldsInField(&descriptor, field_descriptor, "fields_by_name");
      }

      FixContainingTypeInDescriptor(descriptor, containing_descriptor);
      for (int i = 0; i < descriptor.enum_type_count(); ++i) {
        const EnumDescriptor& enum_descriptor = *descriptor.enum_type(i);
        FixContainingTypeInDescriptor(enum_descriptor, &descriptor);
      }
      for (int i = 0; i < descriptor.oneof_decl_count(); ++i) {
        map<string, string> m;
        const OneofDescriptor* oneof = descriptor.oneof_decl(i);
        m["descriptor_name"] = ModuleLevelDescriptorName(descriptor);
        m["oneof_name"] = oneof->name();
        for (int j = 0; j < oneof->field_count(); ++j) {
          m["field_name"] = oneof->field(j)->name();
          printer_->Print(
              m,
              "$descriptor_name$.oneofs_by_name['$oneof_name$'].fields.append(\n"
              "  $descriptor_name$.fields_by_name['$field_name$'])\n");
          printer_->Print(
              m,
              "$descriptor_name$.fields_by_name['$field_name$'].containing_oneof = "
              "$descriptor_name$.oneofs_by_name['$oneof_name$']\n");
        }
      }
    }

    void MyGenerator::AddMessageToFileDescriptor(const Descriptor& descriptor) const {
      map<string, string> m;
      m["descriptor_name"] = kDescriptorKey;
      m["message_name"] = descriptor.name();
      m["message_descriptor_name"] = ModuleLevelDescriptorName(descriptor);
      const char file_descriptor_template[] =
          "$descriptor_name$.message_types_by_name['$message_name$'] = "
          "$message_descriptor_name$\n";
      printer_->Print(m, file_descriptor_template);
    }

    void MyGenerator::AddEnumToFileDescriptor(
        const EnumDescriptor& descriptor) const {
      map<string, string> m;
      m["descriptor_name"] = kDescriptorKey;
      m["enum_name"] = descriptor.name();
      m["enum_descriptor_name"] = ModuleLevelDescriptorName(descriptor);
      const char file_descriptor_template[] =
          "$descriptor_name$.enum_types_by_name['$enum_name$'] = "
          "$enum_descriptor_name$\n";
      printer_->Print(m, file_descriptor_template);
    }

    void MyGenerator::AddExtensionToFileDescriptor(
        const FieldDescriptor& descriptor) const {
      map<string, string> m;
      m["descriptor_name"] = kDescriptorKey;
      m["field_name"] = descriptor.name();
      const char file_descriptor_template[] =
          "$descriptor_name$.extensions_by_name['$field_name$'] = "
          "$field_name$\n";
      printer_->Print(m, file_descriptor_template);
    }

    // Sets any necessary message_type and enum_type attributes
    // for the version of |field|.
    //
    // containing_type may be NULL, in which case this is a module-level field.
    //
    // dict_name is the name of the dict where we should
    // look the field up in the containing type.  (e.g., fields_by_name
    // or extensions_by_name).  We ignore dict_name if containing_type
    // is NULL.
    void MyGenerator::FixForeignFieldsInField(const Descriptor* containing_type,
                                            const FieldDescriptor& field,
                                            const string& dict_name) const {
      const string field_referencing_expression = FieldReferencingExpression(
          containing_type, field, dict_name);
      map<string, string> m;
      m["field_ref"] = field_referencing_expression;
      const Descriptor* foreign_message_type = field.message_type();
      if (foreign_message_type) {
        m["foreign_type"] = ModuleLevelDescriptorName(*foreign_message_type);
        printer_->Print(m, "$field_ref$.message_type = $foreign_type$\n");
      }
      const EnumDescriptor* enum_type = field.enum_type();
      if (enum_type) {
        m["enum_type"] = ModuleLevelDescriptorName(*enum_type);
        printer_->Print(m, "$field_ref$.enum_type = $enum_type$\n");
      }
    }

    // Returns the module-level expression for the given FieldDescriptor.
    // Only works for fields in the .proto file this Generator is generating for.
    //
    // containing_type may be NULL, in which case this is a module-level field.
    //
    // dict_name is the name of the dict where we should
    // look the field up in the containing type.  (e.g., fields_by_name
    // or extensions_by_name).  We ignore dict_name if containing_type
    // is NULL.
    string MyGenerator::FieldReferencingExpression(
        const Descriptor* containing_type,
        const FieldDescriptor& field,
        const string& dict_name) const {
      // We should only ever be looking up fields in the current file.
      // The only things we refer to from other files are message descriptors.
      GOOGLE_CHECK_EQ(field.file(), file_) << field.file()->name() << " vs. "
                                    << file_->name();
      if (!containing_type) {
        return field.name();
      }
      return strings::Substitute(
          "$0.$1['$2']",
          ModuleLevelDescriptorName(*containing_type),
          dict_name, field.name());
    }

    // Prints containing_type for nested descriptors or enum descriptors.
    template <typename DescriptorT>
    void MyGenerator::FixContainingTypeInDescriptor(
        const DescriptorT& descriptor,
        const Descriptor* containing_descriptor) const {
      if (containing_descriptor != NULL) {
        const string nested_name = ModuleLevelDescriptorName(descriptor);
        const string parent_name = ModuleLevelDescriptorName(
            *containing_descriptor);
        printer_->Print(
            "$nested_name$.containing_type = $parent_name$\n",
            "nested_name", nested_name,
            "parent_name", parent_name);
      }
    }

    // Prints statements setting the message_type and enum_type fields in the
    // descriptor objects we've already output in ths file.  We must
    // do this in a separate step due to circular references (otherwise, we'd
    // just set everything in the initial assignment statements).
    void MyGenerator::FixForeignFieldsInDescriptors() const {
      for (int i = 0; i < file_->message_type_count(); ++i) {
        FixForeignFieldsInDescriptor(*file_->message_type(i), NULL);
      }
      for (int i = 0; i < file_->message_type_count(); ++i) {
        AddMessageToFileDescriptor(*file_->message_type(i));
      }
      for (int i = 0; i < file_->enum_type_count(); ++i) {
        AddEnumToFileDescriptor(*file_->enum_type(i));
      }
      for (int i = 0; i < file_->extension_count(); ++i) {
        AddExtensionToFileDescriptor(*file_->extension(i));
      }
      printer_->Print("\n");
    }

    // We need to not only set any necessary message_type fields, but
    // also need to call RegisterExtension() on each message we're
    // extending.
    void MyGenerator::FixForeignFieldsInExtensions() const {
      // Top-level extensions.
      for (int i = 0; i < file_->extension_count(); ++i) {
        FixForeignFieldsInExtension(*file_->extension(i));
      }
      // Nested extensions.
      for (int i = 0; i < file_->message_type_count(); ++i) {
        FixForeignFieldsInNestedExtensions(*file_->message_type(i));
      }
      printer_->Print("\n");
    }

    void MyGenerator::FixForeignFieldsInExtension(
        const FieldDescriptor& extension_field) const {
      GOOGLE_CHECK(extension_field.is_extension());
      // extension_scope() will be NULL for top-level extensions, which is
      // exactly what FixForeignFieldsInField() wants.
      FixForeignFieldsInField(extension_field.extension_scope(), extension_field,
                              "extensions_by_name");

      map<string, string> m;
      // Confusingly, for FieldDescriptors that happen to be extensions,
      // containing_type() means "extended type."
      // On the other hand, extension_scope() will give us what we normally
      // mean by containing_type().
      m["extended_message_class"] = ModuleLevelMessageName(
          *extension_field.containing_type());
      m["field"] = FieldReferencingExpression(extension_field.extension_scope(),
                                              extension_field,
                                              "extensions_by_name");
      printer_->Print(m, "$extended_message_class$.RegisterExtension($field$)\n");
    }

    void MyGenerator::FixForeignFieldsInNestedExtensions(
        const Descriptor& descriptor) const {
      // Recursively fix up extensions in all nested types.
      for (int i = 0; i < descriptor.nested_type_count(); ++i) {
        FixForeignFieldsInNestedExtensions(*descriptor.nested_type(i));
      }
      // Fix up extensions directly contained within this type.
      for (int i = 0; i < descriptor.extension_count(); ++i) {
        FixForeignFieldsInExtension(*descriptor.extension(i));
      }
    }

    // Returns a expression that instantiates a EnumValueDescriptor
    // object for the given C++ descriptor.
    void MyGenerator::PrintEnumValueDescriptor(
        const EnumValueDescriptor& descriptor) const {
      // More circular references.  ::sigh::
      string options_string;
      descriptor.options().SerializeToString(&options_string);
      map<string, string> m;
      m["name"] = descriptor.name();
      m["index"] = SimpleItoa(descriptor.index());
      m["number"] = SimpleItoa(descriptor.number());
      m["options"] = OptionsValue("EnumValueOptions", options_string);
      printer_->Print(
          m,
          "_descriptor.EnumValueDescriptor(\n"
          "  name='$name$', index=$index$, number=$number$,\n"
          "  options=$options$,\n"
          "  type=None)");
    }

    // Returns a expression that calls descriptor._ParseOptions using
    // the given descriptor class name and serialized options protobuf string.
    string MyGenerator::OptionsValue(
        const string& class_name, const string& serialized_options) const {
      if (serialized_options.length() == 0 || GeneratingDescriptorProto()) {
        return "None";
      } else {
        string full_class_name = "descriptor_pb2." + class_name;
    //##!PY25    return "_descriptor._ParseOptions(" + full_class_name + "(), b'"
    //##!PY25        + CEscape(serialized_options)+ "')";
        return "_descriptor._ParseOptions(" + full_class_name + "(), _b('"  //##PY25
            + CEscape(serialized_options)+ "'))";  //##PY25
      }
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
            // defined and/or imported (see FixForeignFieldsInDescriptors()).
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

    bool MyGenerator::GeneratingDescriptorProto() const {
      return file_->name() == "google/protobuf/descriptor.proto";
    }

    // This name is module-qualified iff the given descriptor describes an
    // entity that doesn't come from the current file.
    template <typename DescriptorT>
    string MyGenerator::ModuleLevelDescriptorName(
        const DescriptorT& descriptor) const {
      // We currently don't worry about collisions with underscores in the type
      // names, so these would collide in nasty ways if found in the same file:
      //   OuterProto.ProtoA.ProtoB
      //   OuterProto_ProtoA.ProtoB  # Underscore instead of period.
      // As would these:
      //   OuterProto.ProtoA_.ProtoB
      //   OuterProto.ProtoA._ProtoB  # Leading vs. trailing underscore.
      // (Contrived, but certainly possible).
      //
      // The C++ implementation doesn't guard against this either.  Leaving
      // it for now...
      string name = NamePrefixedWithNestedTypes(descriptor, "_");
      UpperString(&name);
      // Module-private for now.  Easy to make public later; almost impossible
      // to make private later.
      name = "_" + name;
      // We now have the name relative to its own module.  Also qualify with
      // the module name iff this descriptor is from a different .proto file.
      if (descriptor.file() != file_) {
        name = ModuleName(descriptor.file()->name()) + "." + name;
      }
      return name;
    }

    // Returns the name of the message class itself, not the descriptor.
    // Like ModuleLevelDescriptorName(), module-qualifies the name iff
    // the given descriptor describes an entity that doesn't come from
    // the current file.
    string MyGenerator::ModuleLevelMessageName(const Descriptor& descriptor) const {
      string name = NamePrefixedWithNestedTypes(descriptor, ".");
      if (descriptor.file() != file_) {
        name = ModuleName(descriptor.file()->name()) + "." + name;
      }
      return name;
    }

    // Returns the unique module-level identifier given to a service
    // descriptor.
    string MyGenerator::ModuleLevelServiceDescriptorName(
        const ServiceDescriptor& descriptor) const {
      string name = descriptor.name();
      UpperString(&name);
      name = "_" + name;
      if (descriptor.file() != file_) {
        name = ModuleName(descriptor.file()->name()) + "." + name;
      }
      return name;
    }

    // Prints standard constructor arguments serialized_start and serialized_end.
    // Args:
    //   descriptor: The cpp descriptor to have a serialized reference.
    //   proto: A proto
    // Example printer output:
    // serialized_start=41,
    // serialized_end=43,
    //
    template <typename DescriptorT, typename DescriptorProtoT>
    void MyGenerator::PrintSerializedPbInterval(
        const DescriptorT& descriptor, DescriptorProtoT& proto) const {
      descriptor.CopyTo(&proto);
      string sp;
      proto.SerializeToString(&sp);
      int offset = file_descriptor_serialized_.find(sp);
      GOOGLE_CHECK_GE(offset, 0);

      printer_->Print("serialized_start=$serialized_start$,\n"
                      "serialized_end=$serialized_end$,\n",
                      "serialized_start", SimpleItoa(offset),
                      "serialized_end", SimpleItoa(offset + sp.size()));
    }

    namespace {
    void PrintDescriptorOptionsFixingCode(const string& descriptor,
                                          const string& options,
                                          io::Printer* printer) {
      printer->Print(
          "$descriptor$.has_options = True\n"
          "$descriptor$._options = $options$\n",
          "descriptor", descriptor, "options", options);
    }
    }  // namespace

    // Prints expressions that set the options field of all descriptors.
    void MyGenerator::FixAllDescriptorOptions() const {
      // Prints an expression that sets the file descriptor's options.
      string file_options = OptionsValue(
          "FileOptions", file_->options().SerializeAsString());
      if (file_options != "None") {
        PrintDescriptorOptionsFixingCode(kDescriptorKey, file_options, printer_);
      }
      // Prints expressions that set the options for all top level enums.
      for (int i = 0; i < file_->enum_type_count(); ++i) {
        const EnumDescriptor& enum_descriptor = *file_->enum_type(i);
        FixOptionsForEnum(enum_descriptor);
      }
      // Prints expressions that set the options for all top level extensions.
      for (int i = 0; i < file_->extension_count(); ++i) {
        const FieldDescriptor& field = *file_->extension(i);
        FixOptionsForField(field);
      }
      // Prints expressions that set the options for all messages, nested enums,
      // nested extensions and message fields.
      for (int i = 0; i < file_->message_type_count(); ++i) {
        FixOptionsForMessage(*file_->message_type(i));
      }
    }

    // Prints expressions that set the options for an enum descriptor and its
    // value descriptors.
    void MyGenerator::FixOptionsForEnum(const EnumDescriptor& enum_descriptor) const {
      string descriptor_name = ModuleLevelDescriptorName(enum_descriptor);
      string enum_options = OptionsValue(
          "EnumOptions", enum_descriptor.options().SerializeAsString());
      if (enum_options != "None") {
        PrintDescriptorOptionsFixingCode(descriptor_name, enum_options, printer_);
      }
      for (int i = 0; i < enum_descriptor.value_count(); ++i) {
        const EnumValueDescriptor& value_descriptor = *enum_descriptor.value(i);
        string value_options = OptionsValue(
            "EnumValueOptions", value_descriptor.options().SerializeAsString());
        if (value_options != "None") {
          PrintDescriptorOptionsFixingCode(
              StringPrintf("%s.values_by_name[\"%s\"]", descriptor_name.c_str(),
                           value_descriptor.name().c_str()),
              value_options, printer_);
        }
      }
    }

    // Prints expressions that set the options for field descriptors (including
    // extensions).
    void MyGenerator::FixOptionsForField(
        const FieldDescriptor& field) const {
      string field_options = OptionsValue(
          "FieldOptions", field.options().SerializeAsString());
      if (field_options != "None") {
        string field_name;
        if (field.is_extension()) {
          if (field.extension_scope() == NULL) {
            // Top level extensions.
            field_name = field.name();
          } else {
            field_name = FieldReferencingExpression(
                field.extension_scope(), field, "extensions_by_name");
          }
        } else {
          field_name = FieldReferencingExpression(
              field.containing_type(), field, "fields_by_name");
        }
        PrintDescriptorOptionsFixingCode(field_name, field_options, printer_);
      }
    }

    // Prints expressions that set the options for a message and all its inner
    // types (nested messages, nested enums, extensions, fields).
    void MyGenerator::FixOptionsForMessage(const Descriptor& descriptor) const {
      // Nested messages.
      for (int i = 0; i < descriptor.nested_type_count(); ++i) {
        FixOptionsForMessage(*descriptor.nested_type(i));
      }
      // Enums.
      for (int i = 0; i < descriptor.enum_type_count(); ++i) {
        FixOptionsForEnum(*descriptor.enum_type(i));
      }
      // Fields.
      for (int i = 0; i < descriptor.field_count(); ++i) {
        const FieldDescriptor& field = *descriptor.field(i);
        FixOptionsForField(field);
      }
      // Extensions.
      for (int i = 0; i < descriptor.extension_count(); ++i) {
        const FieldDescriptor& field = *descriptor.extension(i);
        FixOptionsForField(field);
      }
      // Message option for this message.
      string message_options = OptionsValue(
          "MessageOptions", descriptor.options().SerializeAsString());
      if (message_options != "None") {
        string descriptor_name = ModuleLevelDescriptorName(descriptor);
        PrintDescriptorOptionsFixingCode(descriptor_name,
                                         message_options,
                                         printer_);
      }
    }

}  // namespace my
