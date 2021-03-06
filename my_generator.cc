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

std::vector<const Descriptor* > getTopLevelMessages(const FileDescriptor* file){
	std::set<const Descriptor*> msgSet;
	for (int i = 0; i < file->message_type_count(); ++i) {
		msgSet.insert(file->message_type(i));
	}

	std::set<const Descriptor*> dependedByOthers;
	for (int i = 0; i < file->message_type_count(); ++i) {
		const Descriptor* msg = file->message_type(i);
		for(int j = 0; j < msg->field_count(); ++j){
			const FieldDescriptor* field = msg->field(j);
			if(field->type() == FieldDescriptor::Type::TYPE_MESSAGE){
				const Descriptor* fieldDesc = field->message_type();
				if(msgSet.count(fieldDesc) > 0) {
					dependedByOthers.insert(fieldDesc);
				}
			}
		}
	}

	std::vector<const Descriptor* > result;
	for (int i = 0; i < file->message_type_count(); ++i) {
		if(dependedByOthers.count(file->message_type(i)) == 0)
			result.push_back(file->message_type(i));
	}
	return result;
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

	FileDescriptorProto fdp;
	file_->CopyTo(&fdp);
	fdp.SerializeToString(&file_descriptor_serialized_);

	std::vector<const Descriptor* > msgs = getTopLevelMessages(file_);
	for (int i = 0; i < msgs.size(); ++i) {
		const string outputFileName = msgs[i]->name() + ".json";
		scoped_ptr<io::ZeroCopyOutputStream> output(context->Open(outputFileName));
		GOOGLE_CHECK(output.get());
		io::Printer printer(output.get(), '$');
		printer_ = &printer;
		PrintDescriptor(*msgs[i]);
		printer_->Print("\n");
		if(printer.failed()) return false;
	}

	return true;
}

string MyGenerator::GetTypeName(const google::protobuf::FieldDescriptor::Type& type) const {
	if(dataTypeNameMap.count(type)){
		return dataTypeNameMap.at(type);
	}
	return "NULL";
}

//Used by Generate
void MyGenerator::PrintDescriptor(const Descriptor& message_descriptor) const {
	//        printer_->Print("\n");
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
	printer_->Print("$name$ : ", "name", FieldNameConverter(field.name()));
	if(field.is_repeated()){
		//array of primitive types or message
		printer_->Print("[\n");
		printer_->Indent();
	}
	if(field.type() == FieldDescriptor::Type::TYPE_MESSAGE){
		if(is_extension){
			printer_->Print(" // extension field");
		}
		//message
		PrintDescriptor(*field.message_type());
	}else {
		printer_->Print("$type$", "type", GetTypeName(field.type()));
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
	//      printer_->Print("\n");
}

}  // namespace my
