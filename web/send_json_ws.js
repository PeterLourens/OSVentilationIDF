var gateway = 'ws://' + window.location.hostname + ':/ws';
var websocket;
var page;

// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
}

function initWebSocket() {

    //console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

// When websocket is established, call the get_json() function
function onOpen(event) {

    //console.log('Connection opened');
    get_json();
    const page = document.getElementById("page_name");

    if (page && page.innerHTML === "index") {
        console.log('Page is index so refresh every 30sec')
        setInterval(get_json, 30000);
    }
    if (page && page.innerHTML === "stats") {
        console.log('Page is stats so refresh every 30sec');
        setInterval(get_json, 30000);
    }
}

// Request to server for readings
function get_json() {

    const page = document.getElementById("page_name");

    if (page && page.innerHTML !== "webserial") {
        //console.log('Page name is not webserial so send stop first');
        //websocket.send("Stop"); // If webserial is sending data, stop it first
    }

    websocket.send(page.innerHTML);
}

function onClose(event) {

    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Function that receives the message from the ESP32 with the readings and matches ids of the json with ids in the html
function onMessage(event) {

    const page = document.getElementById("page_name");

    console.log(event.data);

    if (page && page.innerHTML === "stats") {

        //console.log(event.data);
        let msg = event.data;
        document.getElementById("stats").textContent = msg;
    }

    else if (page && page.innerHTML === "webserial") {

        //console.log(event.data);
        var data = event.data.split("<").join("&lt;").split(">").join("&gt;").split("\"").join("&quot;");
        document.getElementById('log').innerHTML += '<div><pre>' + data + '</pre></div>';
        var objDiv = document.getElementById("log");
        objDiv.scrollTop = objDiv.scrollHeight;
    }

    else {

        //console.log(event.data);
        var myObj = JSON.stringify(event.data);
        var myObj = JSON.parse(event.data);
        var keys = Object.keys(myObj);

        for (const key in myObj) {
            const element = document.getElementById(key);
            if (element) {

                // For text boxes
                if (element && (element.tagName === "INPUT" || element.tagName === "TEXTAREA")) {
                    element.value = myObj[key];
                }

                // For span element
                if (element && element.tagName === "SPAN") {
                    element.textContent = myObj[key];
                }

                //For tables where each cell has unique id
                if (element && (element.tagName === "TD" || element.tagName === "TH")) {
                    element.textContent = myObj[key];
                }

                //For dropdown menus
                if (element && element.tagName === "SELECT") {
                    const targetValue = myObj[key];
                    for (let option of element.options) {
                        option.selected = option.value === targetValue;
                    }
                }

                // For checkboxes
                if (element && element.type === "checkbox") {
                    element.checked = Boolean(myObj[key]); // Sets checkbox to checked
                }
            }
        }
    }
};

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







