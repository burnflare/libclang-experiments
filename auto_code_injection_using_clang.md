#Auto code injection using libclang

##Motivation
I've always been fascinated by IDEs. Long have I wondered how do they what they do: Syntax highlighting, code completion, method refactoring and so much more. Recently, I had a bunch of time on my hands and I decided to figure out how an IDE works it's magic. I chose to play around with XCode because that's my favourite IDE.

Here's the challenge I presented to myself: Given any typical modern iOS project, use the IDE's AST(Abstract Syntax Tree) parsing tools to insert a bunch of code into a predetermined method. To keep this simple, we'll add code to an app's `application:didFinishLaunchingWithOptions` since we can almost always gaurentee that method would exist. So I would like this turn this:

	- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary 	*)launchOptions {
    	// Override point for customization after application launch.
	    return YES;
	}
into:
	
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
So fun.

First things first, I was pretty confident that Xcode was relying on some extra framework/tool to get it's magic done but I was not sure what it is. I tried `spindump` and `iosnoop` on the Xcode process but that didn't reveal anything interesting. Then I tried to sample the Xcode process by running `sample Xcode` in the Terminal. On top of showing all current call stacks of the specified process, `sample` also lists out all the binary images(Frameworks, Static and dynamic libraries) that Xcode has loaded or linked to. Most of the images here were unintersting but one of them caught my attention:

`       0x103002000 -        0x103a94fff +libclang.dylib (600.0.54) <21EB2141-3192-33E4-8641-8CD0F9DA0B20> /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib
`

Further googling mojo revealed that libclang was exactly what I was looking for. The LLVM project trivially describes [libclang](http://clang.llvm.org/doxygen/group__CINDEX.html) as "a stable high level C interface to clang". If you don't already know, Clang is modern compiler for C, C++ and Objective-C that uses LLVM as it's backend. Clang was originally sarted by Apple as a modern replacement to the 25 year old, very much hacked, recursively named, GNU Compiler Collection(GCC). The Clang project is also now stable enough to be the primary compiler for all iOS/Mac apps for the past few years. And libclang seemed like a way to 'talk' to Clang. Perfect, exactly what I wanted.

Unfortunately, libclang isn't very easy to use. Its website is just a simple doxygen page with no usage or sample code. Unable to find sample code anywhere on the internet, it was painful, frustrating and I made a lot of mistakes all over the place. This post aims to save you time and a bunch of mistakes I made while trying to tame down libclang.

Alright, let's begin the tutorial!

##Clone repo
Let's clone the repo

	git clone git@github.com:burnflare/libclang-experiments.git
	cd libclang-experiments

Although Xcode comes with a precompiled version of libclang built-in, we still would need to get our headers from the Clang project's repo(Try to make sure you're following the same dir structure as me here)
	
	git clone http://llvm.org/git/llvm.git
	cd llvm/tools
	git clone http://llvm.org/git/clang.git
	
##Configure Xcode
Now, let's verify that the `libclang-experiments` project is in a valid state, ensuring it's linked to all the right binaries and header paths. If you're trying to get libclang working on your own project, you should reproduce the steps mentioned in this section.

In the project navigator, click on your project, then click on *Build Phases* in the main window. Expand the *Link Binary with Libraries* disclosure, click on the *+* and choose *Add Other...*. Thankfully we don't have to build our own version of libclang.dylib as Xcode comes bundled with one. We can link directly against that! Hit ⌘⇧G and paste this in and click *Open*

	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib
	
Next, move on to the *Build Settings* section and do the following

- Add a new Runpath Search Paths: `$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib`
	- `libclang.dylib` relies heavily on other libraries and it's complete paths are not known during `libclang.dylib`'s creation. It replies on the runtime's dynamic loader to find these libraries so we'll have to provide it with an additional path to search through.
	- `$(DEVELOPER_DIR)` is an Xcode variable that points to `/Applications/Xcode.app/Contents/Developer` or wherever Xcode is installed.
- Add a new Header Search Paths: `$(SRCROOT)/llvm/tools/clang/include` (Resursive)
	- We checked-out LLVM&Clang so that we could use some of it's headers, let's point to the ones we care about
	- `$(SRCROOT)` is a Xcode variable that points to the root of this project. For me, that's `/Users/vishnu/dev/libclang-experiments`. Obviously Your Roots Will Vary(YRWV).
- Add a new Library Search Paths: `$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib`
	- Even though we've 'added' `libclang.dylib` into our Xcode's project navigator, we still need to tell the compiler to look for dynamic libraries in that search path or else it won't find it.
- Enabe Modules(C and Objective-C) - Set this to No.
	
##Explaining source code
The original draft of this project was written in minimal C and mostly Objective-C -- I have an allergy for C, the language, it gives me cooties. But after some deliberation, I decided to refactor the entire app in C as going back and forth between C and Obj-C data types just added more muck to the code for little benefit. And C's not ***that*** bad :P


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

Importing the header `clang-c/Index.h` that lives in our llvm project that we checked out. This header recursively includes everything else we would need to play with libclang

	const char * args[] = { "-c", "-arch", "i386",
    	"-isysroot", "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk",
	    "-I", "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/6.0/include",
    	"-Wno-objc-property-implementation"};

Clang loves to eat all the arguments for breakfast, lunch and dinner. If you want to have fun, [take a look](https://www.dropbox.com/s/zls6pdfhrsuxiqa/Screenshot%202014-11-03%2011.27.09.png?dl=0) at the default list of arguments Xcode sends Clang whenever it tries to build. Have fun understanding that!

I tried to be as minimal as possible with my Clang arguments. Tried a bunch of permurations with all kinds of stuff and and this is what I ended up with:

- `-c`: Expect C, the language.
- `-arch i386`: Expect architecture x86.
- `isysroot <path>`: Root directory for the compiler. Usually you want this to point to the root SDK you want to link against. I'm using the iPhoneSumulator SDK here since we're running this on a x86 CPU.
- `-I <path>`: Add the path to the compiler's include search path.
- `-Wno-objc-property-implementation`: Surpressing a frequent warning that shows up while compiling some of Apple's iOS8 headers.

Fun, right? Ok moving on!

	CXTranslationUnit translationUnit;

A CXIndex consists of multiple Translation Units. A single translation unit is typically used to represent a single source file. I'm defining the translation unit globally here so that methods outside `main()` can use it.

	void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration);
	static IndexerCallbacks indexerCallbacks = {
	    .indexDeclaration = m_indexDeclaration,
	};
	
When we get libclang to parse through our source file, we can implement various callbacks that would triggered. In this project, we only care about the `IndexerCallbacks.indexDeclaration` callback. It's also possible to implement a callback whenever the preprocessor includes a file, more on the other IndexerCallbacks callbacks [here](http://clang.llvm.org/doxygen/structIndexerCallbacks.html)
	
	const char *methodToFind = "application:didFinishLaunchingWithOptions:";
	const char *injectCode = "NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];\n\tif (![defaults objectForKey:@\"firstRun\"])\n\t\t[defaults setObject:[NSDate date] forKey:@\"firstRun\"];\n\t\t// First run!\n\t} else {\n\t\t// Not first run!\n\t}\n\t";
	
`methodToFind` is the signature of the method we're looking for.  
`injectCode` is the escaped, nicely formatted code snippet we're trying to inject into our `AppDelegate.m`.
	
	int main(int argc, const char * argv[]) {
	    CXIndex index = clang_createIndex(1, 1);
	    
	    const char *sourceFile = "/Users/vishnu/Desktop/FlappyCode/FlappyCode/AppDelegate.m";
	    
	    if (!index) {
	        printf("Couldn't create CXIndex");
	        return 0;
	    }
	    
Intialize an empty CXIndex, to get things going. You would notice here that I've decided to hardcode the path to my `AppDelegate.m`, a better programmer would choose to retrieve this from `argv[]` or user input.
	    
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
	    
Initializing the single translation unit we'll be using for our project. The first four arguments pass in the CXIndex, path to source file, Clang arguments array and size of that array respectively. The 5th & 6th argument is used to send files to libclang that have not been saved to disk yet. I'm guessing IDEs(like Xcode) using this to get syntax highighting for code as you're typing on the fly. The last parameter is used to send in special options for the parsing. An interesting option I found here was `CXTranslationUnit_Incomplete` which would tell the parser that we're working with an intensionally incompelte translation unit here, proceed proudly!
	    
	    CXIndexAction action = clang_IndexAction_create(index);
	    clang_indexTranslationUnit(action, NULL, &indexerCallbacks, sizeof(indexerCallbacks), CXIndexOpt_SuppressWarnings, translationUnit);
	    
	    clang_disposeIndex(index);
	    clang_disposeTranslationUnit(translationUnit);
		clang_IndexAction_dispose(action);
	    return 0;
	}
	
Now that we have a translation unit representing our source code, we'll be getting libclang to run an index through it, triggering our above-mentioned callback methods when necessary. CXIndexAction is used to represent an indexing session.

libclang comes with plenty of convenience methods to clean up memory after ourselves, so confidently destroy those CXStuff when you're doing using them!
	
	void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration) {
	    if (declaration->cursor.kind == CXCursor_ObjCInstanceMethodDecl) {
	        if (strcmp(declaration->entityInfo->name, methodToFind) == 0) {
				
From my (weak) understanding, libclang's indexer would call `m_indexDeclaration()` whenever a new declaration has been discovered serially. This includes all of the languages' primitive delarations, Darwin related declarations, Obj-C language declarations, CoreGraphics declarations, SDK declarations and more. Running a counter shows that this method is invoked over 21,000 times.

Thankfully there is a easy way to filter to only the kinds of declarations we care about. In this case, we only care about Objective-C instance method so let's filter that our. You can see the entire list of declaration types [here](http://clang.llvm.org/doxygen/group__CINDEX.html#gaaccc432245b4cd9f2d470913f9ef0013)

Once we know it's a Obj-C instance method, we can do a simple `strcmp()` to confirm that it's correct method signature that we care about.
				
	            CXToken *tokens;
	            unsigned int numTokens;
	            CXCursor *cursors = 0;
	            
	            CXSourceRange range = clang_getCursorExtent(declaration->cursor);
	            clang_tokenize(translationUnit, range, &tokens, &numTokens);
	            cursors = (CXCursor *)malloc(numTokens * sizeof(CXCursor));
	            clang_annotateTokens(translationUnit, tokens, numTokens, cursors);
	            
`CXToken`: A token is used to hold a single *token* of source code in it's simplest form. For example, the following code sequence:

	- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	    // Override point for customization after application launch.
	    return YES;
	}
	
would be tokenized into the array:

	-
	(
	BOOL
	)
	application
	:
	(
	UIApplication
	*
	)
	application
	didFinishLaunchingWithOptions
	:
	(
	NSDictionary
	*
	)
	launchOptions
	{
	// Override point for customization after application launch.
	return
	YES
	;
	}

There are 5 *kinds* of CXTokens possible, they are Punctuation, Keyword, Identifier, Literal and Comment. You can retrieve their kinds using `clang_getTokenKind()`. If you want to print out what exact code the token represents use `clang_getTokenSpelling()`

`CXCursor`: A CXCursor contains a cursor representation of an element in the AST. We will be using `clang_annotateTokens()` to map each CXToken in the tokens array to its respective cursors arrays for future cursor manipulaiton. A cursor can be used for many uses, we will be using it specifically to retrieve a CXToken's exact position(line & offset) in a source file.

`clang_getCursorExtent` returns the physical boundaries of the source represented by that cursor. In this example, it return a CXSourceRange that represents the entire `application:didFinishLaunchingWithOptions:` method from it's first character to last.
				
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
					
Using a loop to run through all the initial tokens in the range until we meet a `{`. We want to insert our injection code into the token right after the `{` token, so I'm using a `next` bool here to keep state of that.
					
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
                    
Now we know where we want to insert code at. Let's extract out the file name and offset from the cursor.
					
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
                    
Code to rewrite the source file with the new code injection in between the cursor point.
					
	                    clang_disposeTokens(translationUnit, tokens, numTokens);
	                    break;
	                }
	            }
	        }
	    }
	}

##Flaws & Improvements
- 
- Adding tokens automaticlly

##Conclusion
We live in exciting times.