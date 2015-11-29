//
//  myCodeGenerator.cpp
//  protobuf-ext
//
//  Created by Shoupu Wan on 11/14/15.
//  Copyright © 2015 Shoupu Wan. All rights reserved.
//

#include <stdlib.h>
#include <iostream>
#include "my_generator.h"
#include "google/protobuf/compiler/plugin.h"
//#include "google/protobuf/stubs/strutil.h"

using namespace std;
int main(int argc, char* argv[]) {
   my::MyGenerator generator;
   return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
