var net = require('net');
var dmxlib=require('dmxnet');

var client = new net.Socket();
var gpio_laststate = [0, 0, 0];
var gpio_wpi_num = [0, 2, 3];

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

client.connect(9999, 'localhost', function() {
  console.log('TCP Connection opened');
    client.write('setup 1,24,3;init;');
  receiver.on('data', function(data) {
      /*console.log('on '
            +(data[0]<16?'0':'')+data[0].toString(16)
            +(data[1]<16?'0':'')+data[1].toString(16)
            +(data[2]<16?'0':'')+data[2].toString(16));*/
      for (var i = 0; i < 24; i++) {
        var led = i*3;
        client.write('fill 1,'
        +(data[led]<16?'0':'')+data[led].toString(16)
        +(data[led+1]<16?'0':'')+data[led+1].toString(16)
        +(data[led+2]<16?'0':'')+data[led+2].toString(16)
        +','+i+';');
      }
      client.write('render;');
      for (var i = 0; i < 3; i++) {
        var state = 0;
        if (data[72+i]>100) {
          state = 1;
        }
        if (state!=gpio_laststate[i]) {
          client.write('gpio '
          +gpio_wpi_num[i]+','
          +state+';');
          gpio_laststate[i] = state;
        }
      }
 });

}).on('error', function(err) {
	console.log('TCP Connection Error');
});

client.on('close', function() {
	console.log('TCP Connection closed');
  process.exit(1);
});
