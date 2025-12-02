var gateway = 'ws://' + window.location.hostname + ':/ws';
var websocket;
var page;

// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

// When websocket is established, call the get_json() function
function onOpen(event) {
    console.log('Connection opened');
    //get_json();
    websocket.send("get_sensor_config");
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Function that receives the message from the ESP32 with the readings and matches ids of the json with ids in the html
function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.stringify(event.data);
    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);

    for (const key in myObj) {
        const element = document.getElementById(key);
        //console.log(element);
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

};

