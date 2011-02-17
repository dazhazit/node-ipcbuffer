
var IPCBuffer = require("../lib/ipcbuffer").Buffer;
var net = require("net");

var stdout = console.log;
var stderr = process.stderr.write;

stdout("Started tests on set "+parseInt(process.argv[2],10));

var BUFFSIZE = parseInt(process.argv[3],10);

var timed = new Date(),i;
function timeit(){
    var when = timed;
    timed = new Date();
    return(timed - when);
}

function fillbuf(buf,len){
    var i;
    for(i = 0;i < len;i++){
	buf[i] = (len - i)&255;
    }
}

function testbuf(buf,name,len){
    var i;
    if(!buf){
	return;
    }
    for(i = 1;i < len;i++){
	if(buf[i] != ((len - i)&255)){
	    stderr(name+" buffer Failed consistency check at "+i+" value "+buf[i]+"\n");
	    return;
	}
    }
}

function tests(num){
    timeit();
    var test2 = new IPCBuffer(BUFFSIZE,"*Buffy"+num);
    stdout("test2 POSIX Shared Buffer("+BUFFSIZE+",\"*Buffy\") open "+timeit()/1000+" Seconds");

    var test3 = new IPCBuffer(BUFFSIZE,"buffy"+num+".buf");
    stdout("test3 POSIX Shared File Buffer("+BUFFSIZE+",\"Buffy.buf\") open "+timeit()/1000+" Seconds");

    var test4 = new IPCBuffer(1024,1234+num);
    stdout("test4 1K SYSV Shared Buffer("+4096+",1234) create "+timeit()/1000+" Seconds");

    testbuf(test2,"test2",BUFFSIZE);
    stdout("test2 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test3,"test3",BUFFSIZE);
    stdout("test3 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test4,"test4",1024);
    stdout("test4 buffer compare "+timeit()/1000+" Seconds");
}

tests(parseInt(process.argv[2],10));
