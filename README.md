A protobuf compiler plugin project that converts proto files to schema format in JSON.

design outline
- homogeneous, recursive structure
- prints the field name and the type it represents.
- collapsable JSON like structure
- build a DAG out of the given proto file and print all the heads in the DAG
- make no difference between fields and extensions

protoc protobuf/proto_file.proto --plugin=plugin-test/protoc-gen-my --proto_path=protobuf --my_out=plugin-test-output
