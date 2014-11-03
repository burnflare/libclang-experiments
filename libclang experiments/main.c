//
//  main.c
//  libclang experiments
//
//  Created by Vishnu Prem on 3/11/14.
//  Copyright (c) 2014 Vishnu Prem. All rights reserved.
//

#include <stdio.h>
#include "string.h"
#include "stdlib.h"
#include "clang-c/Index.h"

const char * args[] = { "-c", "-arch", "i386",
    "-isysroot", "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk",
    "-I", "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/6.0/include",
    "-Wno-objc-property-implementation", "-Wno-all"};

CXTranslationUnit translationUnit;

void m_indexDeclaration(CXClientData client_data, const CXIdxDeclInfo *declaration);

static IndexerCallbacks indexerCallbacks = {
    .indexDeclaration = m_indexDeclaration,
};

const char *methodToFind = "application:didFinishLaunchingWithOptions:";
const char *injectCode = "NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];\n\tif (![defaults objectForKey:@\"firstRun\"]){\n\t\t[defaults setObject:[NSDate date] forKey:@\"firstRun\"];\n\t\t// First run!\n\t} else {\n\t\t// Not first run!\n\t}\n\t";

int main(int argc, const char * argv[]) {
    CXIndex index = clang_createIndex(1, 1);
    
    const char *sourceFile = "/Users/vishnu/Desktop/FlappyCode/FlappyCode/AppDelegate.m";
    
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
                                                 CXTranslationUnit_None);
    
    if (!translationUnit) {
        printf("Couldn't create CXTranslationUnit of %s", sourceFile);
        return 0;
    }
    
    CXIndexAction action = clang_IndexAction_create(index);
    clang_indexTranslationUnit(action, NULL, &indexerCallbacks, sizeof(indexerCallbacks), CXIndexOpt_SuppressWarnings, translationUnit);
    
//    clang_disposeIndex(index);
//    clang_disposeTranslationUnit(translationUnit);
//    clang_IndexAction_dispose(action);
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
                    CXString tString = clang_getTokenSpelling(translationUnit, tokens[i]);
                    const char *cString = clang_getCString(tString);
                    if (tKind == CXToken_Punctuation && strcmp(cString, "{") == 0) {
                        next = 1;
                        continue;
                    }
                }
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
                    printf("Method found in %s in line %d, offset %d", clang_getCString(clang_getFileName(file)), line, offset);
                    
                    FILE *f;
                    f = fopen(filename, "r+");
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    
                    char *string = malloc(fsize);
                    fread(string, fsize, 1, f);
                    fclose(f);
                    
                    FILE *fw;
                    fw = fopen(filename, "w");
                    char *output = malloc(fsize);
                    strncpy(output, string, offset);
                    strcat(output, injectCode);
                    strcat(output, string+offset);
                    printf("%s - %lu %lu",output, sizeof(string), sizeof(injectCode));
                    
//                    fwrite(output, 1, sizeof(output), fw);
//                    fclose(fw);
                    
                    break;
                }
            }
        }
    }
}
