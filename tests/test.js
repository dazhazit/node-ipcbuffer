
var spawn = require("child_process").spawn;
var fs = require("fs");
var IPCBuffer = require("../lib/ipcbuffer").Buffer;

var BUFFSIZE = 1024*1024*16;	// 32MB

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
	    throw(name+" buffer Failed consistency check at "+i+" value "+buf[i]);
	}
    }
}

function tests(num){
    // Control
    timeit();
    var test0 = new Buffer(BUFFSIZE);
    console.log("Control test0 internal Buffer("+BUFFSIZE+") Create buffer "+timeit()/1000+" Seconds");
    fillbuf(test0,BUFFSIZE);
    console.log("Control test0 fill "+timeit()/1000+" Seconds");

    // Create a new internal buffer (Sanity Check);
    var test1 = new IPCBuffer(BUFFSIZE);
    console.log("test1 internal Buffer("+BUFFSIZE+") Create buffer "+timeit()/1000+" Seconds");
    fillbuf(test1,BUFFSIZE);
    console.log("test1 fill "+timeit()/1000+" Seconds");

    var test2 = new IPCBuffer(BUFFSIZE,"*Buffy"+num);
    console.log("test2 POSIX Shared Buffer("+BUFFSIZE+",\"*Buffy\") create "+timeit()/1000+" Seconds");
    fillbuf(test2,BUFFSIZE);
    console.log("test2 fill "+timeit()/1000+" Seconds");

    var test3 = new IPCBuffer(BUFFSIZE,"buffy"+num+".buf");
    console.log("test3 POSIX Shared File Buffer("+BUFFSIZE+",\"Buffy.buf\") create "+timeit()/1000+" Seconds");
    fillbuf(test3,BUFFSIZE);
    console.log("test3 fill "+timeit()/1000+" Seconds");

    var test4 = new IPCBuffer(1024,1234+num);
    console.log("SYSV IPC has limits");
    console.log("test4 1K SYSV Shared Buffer("+4096+",1234) create "+timeit()/1000+" Seconds");
    fillbuf(test4,1024);
    console.log("test4 fill "+timeit()/1000+" Seconds");


    testbuf(test0,"test0",BUFFSIZE);
    console.log("test0 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test1,"test1",BUFFSIZE);
    console.log("test1 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test2,"test2",BUFFSIZE);
    console.log("test2 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test3,"test3",BUFFSIZE);
    console.log("test3 buffer compare "+timeit()/1000+" Seconds");

    testbuf(test4,"test4",1024);
    console.log("test4 buffer compare "+timeit()/1000+" Seconds");

    console.log("Launching Child "+num+" to test sharing");
    var proc = spawn("node",[__dirname+"/test-child.js",num,BUFFSIZE]);
    proc.stdout.on("data",function(data){process.stdout.write("Child "+num+":"+data.toString())});
    proc.stderr.on("data",function(data){process.stderr.write("Error:Child "+num+":"+data.toString())});
    proc.on("exit",function(){console.log("Child "+num+" exited OK");fs.unlink("buffy"+num+".buf")});

}

for(i = 0;i < 5;i++){
    tests(i);
}
