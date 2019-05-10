// Check that Objective-C is able to use a resilient class stub emitted
// by the Swift compiler.

// RUN: %empty-directory(%t)
// RUN: %target-build-swift -emit-library -emit-module -o %t/libfirst.dylib -emit-objc-header-path %t/first.h %S/Inputs/class-stubs-from-objc/first.swift -Xlinker -install_name -Xlinker @executable_path/libfirst.dylib -enable-library-evolution
// RUN: %target-build-swift -emit-library -o %t/libsecond.dylib -emit-objc-header-path %t/second.h -I %t %S/Inputs/class-stubs-from-objc/second.swift -Xlinker -install_name -Xlinker @executable_path/libsecond.dylib -lfirst -L %t -target x86_64-apple-macosx10.15
// RUN: cp %S/Inputs/class-stubs-from-objc/module.map %t/
// RUN: xcrun %clang %s -I %t -L %t -fmodules -fobjc-arc -o %t/main -lfirst -lsecond -Wl,-U,_objc_loadClassref
// RUN: %target-codesign %t/main %t/libfirst.dylib %t/libsecond.dylib
// RUN: %target-run %t/main %t/libfirst.dylib %t/libsecond.dylib

// REQUIRES: executable_test
// REQUIRES: OS=macosx

#import <dlfcn.h>
#import <stdio.h>
#import "second.h"

@implementation DerivedClass (MyCategory)

- (int)instanceMethod {
  return [super instanceMethod] + 1;
}

+ (int)classMethod {
  return [super classMethod] + 1;
}

@end

int main(int argc, const char * const argv[]) {
  // Only test the new behavior on a new enough libobjc.
  if (!dlsym(RTLD_NEXT, "_objc_loadClassref")) {
    fprintf(stderr, "skipping evolution tests; OS too old\n");
    return EXIT_SUCCESS;
  }

  DerivedClass *obj = [[DerivedClass alloc] init];

  {
    long result = [obj instanceMethod];
    printf("[obj instanceMethod] == %ld\n", result);
    if (result != 43)
      exit(EXIT_FAILURE);
  }

  {
    long result = [DerivedClass classMethod];
    printf("[obj classMethod] == %ld\n", result);
    if (result != 31338)
      exit(EXIT_FAILURE);
  }
}
