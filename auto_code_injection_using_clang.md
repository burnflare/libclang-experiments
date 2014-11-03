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

			[[NSUserDefaults standardUserDefaults] synchronize];
			return YES;
	}
So fun.

First things first, I was pretty confident that Xcode was relying on some extra framework/tool to get it's magic done but I was not sure what it is. I sampled my Xcode process by running `sample Xcode` in the Terminal. On top of showing all current call stacks of the specified process, `sample` also lists out all the binary images(Frameworks, Static and dynamic libraries) that Xcode has loaded or linked to. Most of the images here were unintersting but one of them caught my attention:

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
	
##XCode configuration
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
The original draft of this project was written in minimal C and mostly Objective-C -- I have an allergy for C, the language. But after some deliberation, I decided to refactor the entire app in C as going back and forth between C and Obj-C data types just added more muck to the code for little benefit. And C's not ***that*** bad :P


	//
	//  main.c
	//  libclang experiments
	//
	//  Created by Vishnu Prem on 3/11/14.
	//  Copyright (c) 2014 Vishnu Prem. All rights reserved.
	//
	
	#include <stdio.h>
	#include "clang-c/Index.h"
	
	const char * args[] = { "-c","-arch","i386","-isysroot","/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk","-I","/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/6.0/include", "-Wno-objc-property-implementation"};
	
	CXTranslationUnit translationUnit;
	
	void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration);
	
	static IndexerCallbacks indexerCallbacks = {
	    .indexDeclaration = m_indexDeclaration,
	};
	
	const char *methodToFind = "application:didFinishLaunchingWithOptions:";
	const char *injectCode = "[self.window = no];\n\t";
	
	int main(int argc, const char * argv[]) {
	    CXIndex index = clang_createIndex(1, 1);
	    
	    // TODO
	    char* sourceFile = "/Users/vishnu/Desktop/FlappyCode/FlappyCode/AppDelegate.m";
	    
	    if (!index) {
	        printf("Couldn't create CXIndex");
	        return 0;
	    }
	    translationUnit = clang_parseTranslationUnit(index,
	                                                 sourceFile,
	                                                 args,
	                                                 sizeof(args) / sizeof(args[0]),
	                                                 NULL,
	                                                 0,
	                                                 CXTranslationUnit_DetailedPreprocessingRecord);
	    
	    if (!translationUnit) {
	        printf("Couldn't create CXTranslationUnit of %s", sourceFile);
	        return 0;
	    }
	    
	    CXIndexAction action = clang_IndexAction_create(index);
	    clang_indexTranslationUnit(action, NULL, &indexerCallbacks, sizeof(indexerCallbacks), CXIndexOpt_SuppressWarnings, translationUnit);
	    
	    clang_disposeIndex(index);
	    return 0;
	}
	
	void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration) {
	    if (declaration->cursor.kind == CXCursor_ObjCInstanceMethodDecl) {
	        if (strcmp(declaration->entityInfo->name, methodToFind) == 0) {
	            CXToken *tokens;
	            unsigned int numTokens;
	            CXCursor *cursors = 0;
	            
	            CXSourceRange range = clang_getCursorExtent(declaration->cursor);
	            clang_tokenize(translationUnit, range, &tokens, &numTokens);
	            cursors = (CXCursor *)malloc(numTokens * sizeof(CXCursor));
	            clang_annotateTokens(translationUnit, tokens, numTokens, cursors);
	            
	            int next = 0;
	            for(int i=0; i < numTokens; i++) {
	                if (next == 0) {
	                    CXTokenKind tKind = clang_getTokenKind(tokens[i]);
	                    const char *tString = clang_getCString(clang_getTokenSpelling(translationUnit, tokens[i]));
	                    if (tKind == CXToken_Punctuation && strcmp(tString, "{") == 0) {
	                        next = 1;
	                        continue;
	                    }
	                } else {
	                    CXFile file;
	                    unsigned line;
	                    unsigned column;
	                    unsigned offset;
	                    
	                    clang_getSpellingLocation(clang_getCursorLocation(cursors[i+1]),
	                                              &file,
	                                              &line,
	                                              &column,
	                                              &offset);
	                    
	                    const char* filename = clang_getCString(clang_getFileName(file));
	                    printf("Method found in %s in line %d, offset %d, column %d", clang_getCString(clang_getFileName(file)), line, offset, column);
	                    
	                    FILE * f;
	                    f = fopen(filename, "r+");
	                    fseek(f, 0, SEEK_END);
	                    long fsize = ftell(f);
	                    fseek(f, 0, SEEK_SET);
	                    
	                    char *string = malloc(fsize + 1);
	                    fread(string, fsize, 1, f);
	                    fclose(f);
	                    
	                    char *output[sizeof(string)+offset];
	                    strncpy(output, string, offset);
	                    strcat(output, injectCode);
	                    strcat(output, string+offset);
	                    printf(output);
	                    break;
	                }
	            }
	        }
	    }
	}


We live in exciting times.