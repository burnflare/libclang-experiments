#Auto code injection using libClang.dylib

I've always been fascinated by IDEs. Long have I wondered how do they what they do: Syntax highlighting, code completion, method refactoring and so much more. Recently, I had a bunch of time on my hands and I decided to figure out how an IDE works it's magic. I chose to play around with XCode because that's my favourite IDE.

Here's the challenge I posed to myself: Given any typical modern iOS project, use the IDE's AST(Abstract Syntax Tree) parsing tools to insert a bunch of code into a predetermined method. To keep this simple, we'll add code to an app's `application:didFinishLaunchingWithOptions` since we can almost always gaurentee that method would exist. So I would like this turn this:

	- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary 	*)launchOptions {
    	// Override point for customization after application launch.
	    return YES;
	}
into:
	
	- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary 	*)launchOptions {
		[Initialize code]
    	// Override point for customization after application launch.
	    return YES;
	}
So fun.

First things first, I was pretty confident that Xcode was relying on some extra framework/tool to get it's magic done but I was not sure what it is. I sampled my Xcode process by running `sample Xcode` in the Terminal. On top of showing all current call stacks of the specified process, `sample` also lists out all the binary images(Frameworks, Static and dynamic libraries) that Xcode has loaded or linked to. Most of the images here were unintersting but one of them caught my attention:

`       0x103002000 -        0x103a94fff +libclang.dylib (600.0.54) <21EB2141-3192-33E4-8641-8CD0F9DA0B20> /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib
`

Further googling mojo revealed that libclang was exactly what I was looking for. The LLVM project trivially describes [libclang](http://clang.llvm.org/doxygen/group__CINDEX.html) as "a stable high level C interface to clang". If you don't already know, Clang is modern compiler for C, C++ and Objective-C that uses LLVM as it's backend. Clang was originally sarted by Apple as a modern replacement to the 25 year old, very much hacked, recursively named, GNU Compiler Collection(GCC). The Clang project is also now stable enough to be the primary compiler for all iOS/Mac apps for the past few years. And libclang seemed like a way to 'talk' to Clang. Perfect, exactly what I wanted.

Unfortunately, libclang isn't very easy to use. Its website is just a simple doxygen page with no usage or sample code. Unable to find sample code anywhere on the internet, it was painful, frustrating and I made mistakes all over the place. Hopefully through this project, you can save some time in your dealings with libclang and help me improve my methods too.

You can find the project here: https://github.com/burnflare/libclang-experiments

We live in exciting times.