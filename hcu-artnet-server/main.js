var net = require('net');
var dmxlib=require('dmxnet');
const SerialPort = require('serialport');
const Readline = require('@serialport/parser-readline');
const port = new SerialPort('/dev/ttyACM0', { baudRate: 9600 });
const parser = port.pipe(new Readline({ delimiter: '\n' }));// Read the port data

var client = new net.Socket();

var dmxnet = new dmxlib.dmxnet({
  verbose: 0, //Verbosity, default 0
  oem: 0, //OEM Code from artisticlicense, default to dmxnet OEM.
  sName: "HafenCity", // 17 char long node description, default to "dmxnet"
  lName: "HafenCity Universität - ArtNet to TCP translator" // 63 char long node description, default to "dmxnet - OpenSource ArtNet Transceiver"
});

var receiver=dmxnet.newReceiver({
  subnet: 0, //Destination subnet, default 0
  universe: 0, //Destination universe, default 0
  net: 0, //Destination net, default 0
});

port.on("open", () => {
  console.log('serial port open');
});parser.on('data', data =>{
  console.log('got word from arduino:', data);
});

client.connect(9998, '192.168.178.30', function() {
    client.write('setup 1,18,3;init;');
  receiver.on('data', function(data) {
      /*console.log('on '
            +(data[0]<16?'0':'')+data[0].toString(16)
            +(data[1]<16?'0':'')+data[1].toString(16)
            +(data[2]<16?'0':'')+data[2].toString(16));*/
      for (var i = 0; i < 18; i++) {
        var led = i*3;
        client.write('fill 1,'
        +(data[led]<16?'0':'')+data[led].toString(16)
        +(data[led+1]<16?'0':'')+data[led+1].toString(16)
        +(data[led+2]<16?'0':'')+data[led+2].toString(16)
        +','+i+';');
      }
      client.write('render;');
      for(var i=25;i < 40;i++){
      port.write(i+'c'+data[i]+'w\n', (err) => {
         if (err) {
            return console.log('Error on write: ', err.message);
         }
         console.log('message written');
      });
  });

}).on('error', function(err) {
	console.log('Connection Error');
});

client.on('close', function() {
	console.log('Connection closed');
});
