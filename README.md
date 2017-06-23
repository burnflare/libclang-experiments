Reposted from my NUS Hacker [digest entry](http://digest.nushackers.org/2014/11/02/libclang-ast-parsing/)
This post references [this](https://github.com/burnflare/libclang-experiments/) Github project.
# Auto Code Injection with libclang
## Motivation
I've always been fascinated by IDEs. Long have I wondered how do they what they do: syntax highlighting, code completion, method refactoring and so much more. Recently, I had a bunch of time on my hands and I decided to figure out how an IDE works its magic. I chose to play around with Xcode because that's my favourite IDE.

Here's the challenge I presented to myself: given any typical modern iOS project, use the IDE's AST (Abstract Syntax Tree) parsing tools to insert a bunch of code into a predetermined method. In this example, we'll add code to an iOS app's `application:didFinishLaunchingWithOptions` since we can almost always guarantee that this method would exist. So I would like to turn this:
```Objective-C
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary 	*)launchOptions {
    // Override point for customization after application launch.
    return YES;
}
```
into:
```Objective-C	
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary 	*)launchOptions {
    // Override point for customization after application launch.
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];  
    if (![defaults objectForKey:@"firstRun"])
        [defaults setObject:[NSDate date] forKey:@"firstRun"];
        // First run!
    } else {
        // Not first run!
    }
}
```
First things first, I was pretty confident that Xcode was relying on some extra framework/tool to get its magic done but I was not sure what it was. I tried `spindump` and `iosnoop` on the Xcode process but that didn't reveal anything interesting. Then I tried to sample the Xcode process by running `sample Xcode` in the Terminal. On top of showing all current call stacks of the specified process, `sample` also lists out all the binary images (Frameworks, Static and dynamic libraries) that Xcode has loaded or linked to. Most of the images here were uninteresting but one of them caught my attention:

	0x103002000 -        0x103a94fff +libclang.dylib (600.0.54) <21EB2141-3192-33E4-8641-8CD0F9DA0B20> /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib

Further googling revealed that libclang was exactly what I was looking for. The LLVM project trivially describes [libclang](http://clang.llvm.org/doxygen/group__CINDEX.html) as "a stable high level C interface to clang". If you don't already know, Clang is modern compiler for C, C++ and Objective-C that uses LLVM as it's backend. The Clang project was originally started in Apple as a modern replacement to the 25 year old, very-much-hacked, recursively named, GNU Compiler Collection (GCC). Clang is also now matured enough to be the primary compiler used for all iOS/Mac apps for the past few years. And libclang seemed like a way to 'talk' to Clang. Perfect, exactly what I wanted.

Unfortunately, libclang isn't very easy to use for someone who has no experience with Clang APIs. Its website is just a simple doxygen page with no instructions or sample code. Unable to find sample code anywhere on the internet, it was a painful, frustrating process and I made a lot of mistakes all over the place trying to get libclang working. This post aims to save you time and a bunch of mistakes I made while trying to tame down libclang. And I'll try to explain some stuff along the way.

Alright, let's begin the tutorial!

## Clone repo
Let's clone the repo
```
git clone https://github.com/burnflare/libclang-experiments.git
cd libclang-experiments
```
Although Xcode comes with a precompiled version of libclang built-in, we still need to get our headers from the Clang project (Try to make sure you're following the same directory structure as I am here)
```
git clone http://llvm.org/git/llvm.git
cd llvm/tools
git clone http://llvm.org/git/clang.git
```
## Configure Xcode
Now, let's verify that the `libclang-experiments` Xcode project is in a valid state, ensuring that it's linked to all the right binaries and header paths. If you're trying to get libclang working on your own project, you should reproduce the steps mentioned in this section.

In the project navigator, click on your project, then click on *Build Phases* in the main window. Expand the *Link Binary with Libraries* disclosure, click on the *+* and choose *Add Other...*. Thankfully, we don't have to build our own version of libclang.dylib (I've spend hours doing that) as Xcode comes bundled with one. We can link directly against that! Hit ⌘⇧G and paste this in and click *Open*
`/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib`
	
Next, move on to the *Build Settings* section and do the following:

- Add a new Runpath Search Paths: `$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib`
	- `libclang.dylib` relies heavily on other libraries and its complete paths are not known on compile time. It relies on the runtime's dynamic loader to find these libraries so we'll have to provide it with an additional path to search through.
	- `$(DEVELOPER_DIR)` is an Xcode variable that points to `/Applications/Xcode.app/Contents/Developer` or wherever Xcode is installed.
- Add a new Header Search Paths: `$(SRCROOT)/llvm/tools/clang/include` (Resursive)
	- We checked-out LLVM&Clang so that we could use some of its headers, so let's point to the ones we care about
	- `$(SRCROOT)` is a Xcode variable that points to the root of this project. For me, that's `/Users/vishnu/dev/libclang-experiments`. Obviously, Your Roots May Vary (YRMV).
- Add a new Library Search Paths: `$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib`
	- Even though we've 'added' `libclang.dylib` into our Xcode's project navigator, we still need to tell the compiler to look for dynamic libraries in that search path or else it won't find it.
- Enable Modules (C and Objective-C) - Set this to No.
	
## Explaining source code
The original draft of this project was written in minimal C and mostly Objective-C. I have an allergy to C, the language (it gives me the shivers). But after some deliberation, I decided to refactor the entire app in C as going back and forth between C and Obj-C types just added more muck to the code for little benefit. And C's not ***that*** bad :P

```C
//
//  main.c
//  libclang experiments
//
//  Created by Vishnu Prem on 3/11/14.
//  Copyright (c) 2014 Vishnu Prem. All rights reserved.
//

#include <stdio.h>
#include "string.h"
#include "clang-c/Index.h"
```

Importing the header `clang-c/Index.h` that lives in our LLVM project that we checked out. This header recursively includes everything else we would need to play with libclang
```
const char * args[] = { "-c", "-arch", "i386",
    "-isysroot", "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk",
    "-I", "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/6.0/include",
    "-Wno-objc-property-implementation"};
```
Clang loves to eat all the arguments for breakfast, lunch and dinner. If you want to have fun, [take a look](https://www.dropbox.com/s/zls6pdfhrsuxiqa/Screenshot%202014-11-03%2011.27.09.png?dl=0) at the default list of arguments Xcode sends Clang whenever it tries to build. Have fun understanding that!

I tried to be as minimal as possible with my Clang arguments. Tried a bunch of permutations with all kinds of stuff and and this is what I ended up with:

- `-c`: Expect C, the language.
- `-arch i386`: Expect architecture x86.
- `isysroot <path>`: Root directory for the compiler. Usually you want this to point to the root SDK you want to link against. I'm using the iPhoneSumulator SDK here since we're running this on a x86 CPU.
- `-I <path>`: Add the path to the compiler's include search path.
- `-Wno-objc-property-implementation`: Suppressing a frequent warning that shows up while compiling some of Apple's iOS8 headers.

Fun, right? Ok moving on!
```C++
CXTranslationUnit translationUnit;
```

A CXIndex consists of multiple Translation Units. A single translation unit is typically used to represent a single source file. I'm defining the translation unit globally here so that methods outside `main()` can use it.
```C++
void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration);
static IndexerCallbacks indexerCallbacks = {
    .indexDeclaration = m_indexDeclaration,
};
```	
When we get libclang to parse through our source file, we can implement various callbacks that will be triggered. In this project, we only care about the `IndexerCallbacks.indexDeclaration` callback. It's also possible to implement a callback whenever the preprocessor includes a file, more on the other IndexerCallbacks callbacks [here](http://clang.llvm.org/doxygen/structIndexerCallbacks.html)
```C++
const char *methodToFind = "application:didFinishLaunchingWithOptions:";
const char *injectCode = "NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];\n\tif (![defaults objectForKey:@\"firstRun\"])\n\t\t[defaults setObject:[NSDate date] forKey:@\"firstRun\"];\n\t\t// First run!\n\t} else {\n\t\t// Not first run!\n\t}\n\t";
```
`methodToFind` is the signature of the method we're looking for.  
`injectCode` is the escaped, nicely formatted code snippet we're trying to inject into our `AppDelegate.m`.
```C++
int main(int argc, const char * argv[]) {
    CXIndex index = clang_createIndex(1, 1);

    const char *sourceFile = "/Users/vishnu/Desktop/FlappyCode/FlappyCode/AppDelegate.m";

    if (!index) {
        printf("Couldn't create CXIndex");
        return 0;
    }
```	    
Initialize an empty CXIndex, to get things started. You will notice here that I've decided to hardcode the path to my `AppDelegate.m`, but a better programmer would choose to retrieve this from `argv[]` or user input.
```C++    
    translationUnit = clang_parseTranslationUnit(index,
                                                 sourceFile,
                                                 args,
                                                 sizeof(args) / sizeof(args[0]),
                                                 NULL,
                                                 0,
                                                 CXTranslationUnit_None);

    if (!translationUnit) {
        printf("Couldn't create CXTranslationUnit of %s", sourceFile);
        return 0;
    }
```	    
Here, we are initialising the single translation unit we'll be using for our project. The first four arguments passed in are the CXIndex, path to source file, Clang arguments array and size of that array respectively. The fifth & sixth argument is used to send files to libclang that have not been saved to disk yet. I'm guessing IDEs (like Xcode) use this to get syntax highighting for code as you're typing on the fly. The last parameter is used to send in [special options](http://clang.llvm.org/doxygen/group__CINDEX__TRANSLATION__UNIT.html#gab1e4965c1ebe8e41d71e90203a723fe9) for the parser. An interesting option I found here was `CXTranslationUnit_Incomplete` which would tell the parser that we're working with an intentionally incomplete translation unit here. Proceed proudly!
```C++    
    CXIndexAction action = clang_IndexAction_create(index);
    clang_indexTranslationUnit(action, NULL, &indexerCallbacks, sizeof(indexerCallbacks), CXIndexOpt_SuppressWarnings, translationUnit);

    clang_disposeIndex(index);
    clang_disposeTranslationUnit(translationUnit);
    clang_IndexAction_dispose(action);
    return 0;
}
```	
Now that we have a translation unit representing our source code, we'll be getting libclang to run an index through it, triggering our above-mentioned callback methods when necessary. CXIndexAction is used to represent an indexing session.

libclang comes with plenty of convenience methods to clean up memory after ourselves, so you can go ahead and destroy those CXStuff when you're doing using them.
```C++	
void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration) {
    if (declaration->cursor.kind == CXCursor_ObjCInstanceMethodDecl) {
        if (strcmp(declaration->entityInfo->name, methodToFind) == 0) {
```				
From my (weak) understanding, libclang's indexer calls `m_indexDeclaration()` whenever a new declaration has been discovered serially in the build process. This includes all of the languages' primitive declarations, Darwin related declarations, Obj-C language declarations, CoreGraphics declarations, SDK declarations and more. Running a counter shows that this method is invoked over 21,000 times.

Thankfully, there's an easy way to filter to only the kinds of declarations we care about. In this case, we only care about Objective-C instance method so let's filter that out. You can see the entire list of declaration types [here](http://clang.llvm.org/doxygen/group__CINDEX.html#gaaccc432245b4cd9f2d470913f9ef0013)

Once we know that it's a Obj-C instance method, we can do a simple `strcmp()` to confirm that its method signature is the one that we care about.
```C++				
    CXToken *tokens;
    unsigned int numTokens;
    CXCursor *cursors = 0;

    CXSourceRange range = clang_getCursorExtent(declaration->cursor);
    clang_tokenize(translationUnit, range, &tokens, &numTokens);
    cursors = (CXCursor *)malloc(numTokens * sizeof(CXCursor));
    clang_annotateTokens(translationUnit, tokens, numTokens, cursors);
```           
`CXToken`: A token is used to hold a single *token* of source code in its simplest form. For example, the following code sequence:
```Objective-C
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Override point for customization after application launch.
    return YES;
}
```	
would be tokenized into the array:

	[01] - -
	[02] - (
	[03] - BOOL
	[04] - )
	[05] - application
	[06] - :
	[07] - (
	[08] - UIApplication
	[09] - *
	[10] - )
	[11] - application
	[12] - didFinishLaunchingWithOptions
	[13] - :
	[14] - (
	[15] - NSDictionary
	[16] - *
	[17] - )
	[18] - launchOptions
	[19] - {
	[20] - // Override point for customization after application launch.
	[21] - return
	[22] - YES
	[23] - ;
	[24] - }

There are 5 possible kinds of CXTokens: Punctuation, Keyword, Identifier, Literal and Comment. You can retrieve their kinds using `clang_getTokenKind()`. If you want to print out the exact code the token represents, use `clang_getTokenSpelling()`

`CXCursor`: A CXCursor contains a cursor representation of an element in the AST. A cursor can be used for many uses, but we will be using it specifically to retrieve a CXToken's exact position (line & offset) in a source file. We will be using `clang_annotateTokens()` to map each CXToken in the tokens array to its respective cursors arrays for future cursor manipulation.

`clang_getCursorExtent` returns the physical boundaries of the source represented by that cursor. In this example, it returns a CXSourceRange that represents the entire `application:didFinishLaunchingWithOptions:` method from the first `'` character to the last `}`.
```C++			
    int next = 0;
    for(int i=0; i < numTokens; i++) {
        if (next == 0) {
            CXTokenKind tKind = clang_getTokenKind(tokens[i]);
            CXString tString = clang_getTokenSpelling(translationUnit, tokens[i]);
            const char *cString = clang_getCString(tString);
            if (tKind == CXToken_Punctuation && strcmp(cString, "{") == 0) {
                next = 1;
                continue;
            }
        }
```					
Using a loop to run through all the initial tokens in the range until we meet a `{`. We want to insert our injection code into the token right after the `{` token, so I'm using a `next` bool here to keep state of that.
```C++
    else {
        CXFile file;
        unsigned line;
        unsigned offset;

        clang_getSpellingLocation(clang_getCursorLocation(cursors[i+1]),
                                  &file,
                                  &line,
                                  NULL,
                                  &offset);

        const char* filename = clang_getCString(clang_getFileName(file));
        printf("\n\nMethod found in %s in line %d, offset %d\n", clang_getCString(clang_getFileName(file)), line, offset);
```
Now we know where we want to insert our code at. Let's extract out the file name and offset out from the cursor.
```C++				
        // File reader
        FILE *fr = fopen(filename, "r");
        fseek(fr, 0, SEEK_END);
        long fsize = ftell(fr);
        fseek(fr, 0, SEEK_SET);

        // Reading file to string
        char *input = malloc(fsize);
        fread(input, fsize, 1, fr);
        fclose(fr);

        // Making an output that is input(start till offset) + code injection + input(offset till end)
        FILE *fw = fopen(filename, "w");
        char *output = malloc(fsize);
        strncpy(output, input, offset);
        strcat(output, injectCode);
        strcat(output, input+offset);

        // Rewrite the whole file with output string
        fwrite(output, fsize, sizeof(output), fw);
        fclose(fw);
```
Code to rewrite the source file with the new code in between the cursor point.
```C++					
                    clang_disposeTokens(translationUnit, tokens, numTokens);
                    break;
                }
            }
        }
    }
}
```
## Flaws & Improvements
This is just my very first attempt in trying to demystify libclang. I've probably just covered 2% of libclang's API and there's so much more it can do. And I've probably made a lot of trivial mistakes in my methodology too.

- Right now, this project requires you to manually point to an `AppDelegate.m` file. It would be much cooler if we could just point to a project folder and this tool would do the rest. Pretty sure it's quite doable by parsing through Xcode's .xcproject file and looking for a main.m file then discovering an `AppDelegate` file from there.
- Right now, after finding out which token I want to insert myself into, I'm using C's ugly `fopen` and `fwrite` APIs to actual do the code insertion for me. I'm pretty sure a competent AST parser like libclang would have the ability for me to programatically create CXTokens and append them into my CXTranslationUnit and get the parser to generate the source file for me. I'm sure this is possible, but I've not discovered it yet, so please tell me if you do!
- I think Xcode's code completion functionality works [through libclang](http://clang.llvm.org/doxygen/group__CINDEX__CODE__COMPLET.html) too, it might be interested to work with those set of APIs next.

## Conclusion
It was very exciting trying to pry open Xcode and look at how its refactoring and code completion tools work. Given some time, I might be able to build my own IDE too, wrapped around libclang.

If you have any thoughts, comments or improvements, feel free to shout at me on [Twitter](http://twitter.com/burnflare), email me at vishnu [at] nushackers [dot] org or create an [issue](https://github.com/burnflare/libclang-experiments/issues/new) on the [Github](https://github.com/burnflare/libclang-experiments/) repo.

We live in exciting times.
