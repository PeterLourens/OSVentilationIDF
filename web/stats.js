var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;

function initWebSocket() {
    console.log("Connecting to " + gateway);
    websocket = new WebSocket(gateway);

    websocket.onopen = function () {
        console.log("WebSocket opened");
        requestStats(); // request immediately
        setInterval(requestStats, 30000); // refresh every 30s
    };

    websocket.onclose = function () {
        console.log("WebSocket closed, retrying...");
        setTimeout(initWebSocket, 2000);
    };

    websocket.onmessage = function (event) {
        let msg = event.data;
        //if (msg.startsWith("STATS:")) {
        // Replace stats block
        document.getElementById("stats").textContent = msg;
        //}
    };
}

function requestStats() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("Stop");
        websocket.send("GetStats");
    }
}

window.addEventListener('load', initWebSocket);
