
var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;

function initWebSocket() {
    console.log("Connecting to " + gateway);
    websocket = new WebSocket(gateway);

    websocket.onopen = function () {
        console.log("WebSocket connected");
        request_index_data(); // request immediately
        setInterval(request_index_data, 10000); // refresh every 10s
    };

    websocket.onclose = function () {
        console.log("WebSocket closed, retrying...");
        setTimeout(initWebSocket, 2000);
    };

    websocket.onmessage = function (event) {
        let data = JSON.parse(event.data);

        // === Update valve table ===
        data.valves.forEach((pos, idx) => {
            let cell = document.getElementById("valve" + idx);
            if (cell) cell.textContent = pos;
        });

        // === Update bus0 sensors ===
        data.bus0.forEach((s, idx) => {
            document.getElementById("bus0_sensor" + idx + "_type").textContent = s.type;
            document.getElementById("bus0_sensor" + idx + "_temp").textContent = s.temp;
            document.getElementById("bus0_sensor" + idx + "_hum").textContent = s.rh;
            document.getElementById("bus0_sensor" + idx + "_co2").textContent = s.co2;
            document.getElementById("bus0_sensor" + idx + "_valve").textContent = s.valve;
            document.getElementById("bus0_sensor" + idx + "_location").textContent = s.location;
            document.getElementById("bus0_sensor" + idx + "_rhs").textContent = s.rhs;
            document.getElementById("bus0_sensor" + idx + "_co2s").textContent = s.co2s;
        });

        // === Update bus1 sensors ===
        data.bus1.forEach((s, idx) => {
            document.getElementById("bus1_sensor" + idx + "_type").textContent = s.type;
            document.getElementById("bus1_sensor" + idx + "_temp").textContent = s.temp;
            document.getElementById("bus1_sensor" + idx + "_hum").textContent = s.rh;
            document.getElementById("bus1_sensor" + idx + "_co2").textContent = s.co2;
            document.getElementById("bus1_sensor" + idx + "_valve").textContent = s.valve;
            document.getElementById("bus1_sensor" + idx + "_location").textContent = s.location;
            document.getElementById("bus1_sensor" + idx + "_rhs").textContent = s.rhs;
            document.getElementById("bus1_sensor" + idx + "_co2s").textContent = s.co2s;
        });

        // Optional: request updates periodically
        setInterval(() => {
            if (websocket.readyState === WebSocket.OPEN) {
                websocket.send("get_index_data");
            }
        }, 10000); // every 10 seconds
    };
}

function request_index_data() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("get_index_data");
    }
}

window.addEventListener('load', initWebSocket);