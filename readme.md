Node.JS Inter Process Communications Buffers
====
___

Premise
----
___
**POSIX and SYSTEM V sharing of buffers between processes.** *(Windows is coming. I Promise)*
For a long time now I've been struggling with passing data between processes. Passing file descriptors to be quite honest has always seemed a bit of a cludge to me.


UNIX, Linux, Solaris, Windows etc all have their own means of passing data between processes very much faster and without the double buffering involved with pipes etc.
This is what *node-ipcbuffer* does.

Implementation
----
___
I've tried my best to make this as a modification to the standard Buffer library so it is a directly compatible API. (In fact mostly it is a copy of the Node.JS buffer lib)


For the moment I've only implemented both POSIX and SYSTEM V IPC Buffer sharing *(but Windows will be on it's way, as the actual API although different does the same things)*.


Hopefully this will end up put back into the main trunk of Node.JS at some point but for the moment this is a direct Drop In replacement for the Buffer object as it is actually only a modification of the original Buffer object from the Node.JS source and directly compatible.


This module supports 4 types of buffering. They are;
* Normal in process memory buffers.
* POSIX Shared in memory only.
* POSIX Shared and backed by a physical file.
* System V Shared in memory only.


The difference between System V and POSIX is that System V uses a numeric ID or key rather than a filename and has limits on size.
Plus you can't save it to a file.

Erm...

**Yes I did say files... this can be used to read and write files very very fast.**
As the OS kernel will take care of the details without you having to call `fs.write()` etc...

Plus again there's no double or tripple buffer overheads when writing.
`buff[1] = "a"` will go directly to byte 1 of the file (eventually when the OS decides it should).
The buffer will already contain the contents of the file if it exists when it's instantiated so `buff[2]` will have valid previous data in it.
Anyone opening the file as a standard file either may or may not see the changes as the OS updates when it sees fit.
However a judicious `delete` or when the buffer dissappears from the scope, it will make it sync back to disk when the buffer object is garbage collected.

This I'm told was the mechanism that the c-lib shared library mechanism used to use to access already loaded libraries quickly.


Usage
---
___
In normal use you would use this exactly the same as you would use `Buffer()` normally.
All you need know is that if you put a filename on the end of the parameters you're going to get a shared buffer.
I've beefed up the parameter parsing a little so it's quite flexible though.


Examples.
`var IPCBuffer = require("ipcbuffer");` - Obviously


*Same as Buffer in Node*
*	`IPCBuffer(length)` - a buffer of length
*	`IPCBuffer(length,encoding)` - set encoding is one of utf8, ascii, binary, base64
*	`IPCBuffer(parent,length)` - create an alias buffer for the entire parent buffer
*	`IPCBuffer(parent,length,offset)` - an alias for a chunk of parent.
*	`IPCBuffer(parent,length,encoding,offset)` - an alias for a chunk of parent.


*The good stuff*
*	`IPCBuffer(length,"*"+filename)` - POSIX virtual buffer. A star before the filename prevents the creation or use of a file.
*	`IPCBuffer(length,filename)` - POSIX page file backed buffer.
*	`IPCBuffer(length,id)` - System V Virtual buffer.
*	`IPCBuffer(length,encoding,filename)` - POSIX page file backed buffer with a set encoding as above.
*I think you can work out the rest of the permutations*


All the other standard buffer operations should work on our shared ones without any difference. The test.js program shows no time penalties whatsoever.

Installation
----
___
Part of this needs compiling with GNU g++. So if you can't do that then I wouldn't bother.
The only binaries I could provide are for a Linux 64bit based system, I don't actually have a Window$ box and I refuse to turn my Amazon EC2 account into a compile farm.


Download the source. Unpack it. Type Make. Simples\*


In the src directory, the waf file contains define parameters for both `__POSIX__` and `__SYSV__`.
If your system doesn't support one or the other then take out the define *(or both and have a copy of the standard Buffer module)*.


Finally
----
___
**Sorry for the panhandling** but I'm broke, unemployed and recently recovered from Kidney Disease *(As in I no longer have a right hand one)*.
If someone see's fit to make a donation to the help Darron work full time on Node.JS enhancements fund. There will be much node goodness to come. I really do mean goodies now I'm up to speed on the inner workings of it.

*(I already have a self written and working Virtual Hosting Web server going (Actually it's meant to do very much more) but the above is going to be one of the enhancements and it will be rewritten. Hence no release as yet.)*


For my next trick. **Semaphores**... and maybe fix that irritating little memory leak bug with `vm.runInContext()`.

