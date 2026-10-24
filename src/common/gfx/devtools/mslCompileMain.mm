// mslcompile: spirvmsl's .metal files compiled by the Metal framework on this Mac, as SDL's Metal driver compiles a
// program's source (newLibraryWithSource with no options) - the compile a Mac without Xcode's Metal tools can make.
//
//   mslcompile <folder>
//
// One line per file that does not compile, or whose library does not hold exactly one function, with Metal's
// message; the warnings of those that do; then a summary. The exit code is 1 if any failed.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("mslcompile <folder>: compiles every .metal in it as SDL's Metal driver would\n");
        return 2;
    }
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            printf("[mslcompile] no Metal device\n");
            return 1;
        }
        NSString *folder = @(argv[1]);
        NSArray<NSString *> *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:folder error:nil];
        int compiled = 0, failed = 0, warned = 0;
        for (NSString *name in [names sortedArrayUsingSelector:@selector(compare:)]) {
            if (![name.pathExtension isEqualToString:@"metal"])
                continue;
            NSString *source = [NSString stringWithContentsOfFile:[folder stringByAppendingPathComponent:name]
                                                         encoding:NSUTF8StringEncoding error:nil];
            NSError *error = nil;
            id<MTLLibrary> library = source != nil ? [device newLibraryWithSource:source options:nil error:&error] : nil;
            if (library == nil) {
                printf("%s: %s\n", name.UTF8String, error != nil ? error.localizedDescription.UTF8String : "not read");
                failed++;
            } else if (library.functionNames.count != 1) {
                printf("%s: %lu functions\n", name.UTF8String, (unsigned long)library.functionNames.count);
                failed++;
            } else {
                if (error != nil) {
                    printf("%s: warnings: %s\n", name.UTF8String, error.localizedDescription.UTF8String);
                    warned++;
                }
                compiled++;
            }
        }
        printf("[mslcompile] %s: %d compiled (%d with warnings), %d failed\n", device.name.UTF8String, compiled,
               warned, failed);
        return failed != 0;
    }
}
