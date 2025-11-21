
function onLoad(event) {
    initWebSocket();
}
var gateway = 'ws://192.168.0.67/ws';
var websocket;
var outCounter = 0;
window.addEventListener('load', onLoad);
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}
function onOpen(event) {
    console.log('Connection opened');
}
function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}
function onMessage(event) {
    console.log(event.data);
    var data = event.data.split("<").join("&lt;").split(">").join("&gt;").split("\"").join("&quot;");
    document.getElementById('log').innerHTML += '<div><pre>' + data + '</pre></div>';
    var objDiv = document.getElementById("log");
    objDiv.scrollTop = objDiv.scrollHeight;
}
function Start() {
    websocket.send('Start');
}
function Stop() {
    websocket.send('Stop');
}
function Clear() {
    document.getElementById('log').innerHTML = '';
}
function SaveToFile() {
    var log_contents = "";
    var lines = document.getElementsByTagName('pre');
    for (var i = 0; i < lines.length; i++) {
        var processed = lines[i].innerHTML.split("&lt;").join("<").split("&gt;").join(">").split("&quot;").join("\"");
        log_contents += processed + "\n";
    }
    SaveFile(log_contents);
}
function SaveFile(log_contents) {
    var a = document.createElement("a");
    file = new Blob([log_contents], { type: "text/plain" });
    a.href = URL.createObjectURL(file);
    a.download = "log.txt";
    a.click();
}
