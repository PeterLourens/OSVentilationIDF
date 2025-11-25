var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;

function initWebSocket() {
    console.log("Connecting to " + gateway);
    websocket = new WebSocket(gateway);

    websocket.onopen = function () {
        console.log("WebSocket connected");
        request_index_data(); // request immediately
        setInterval(request_index_data, 30000); // refresh every 30s
    };

    websocket.onclose = function () {
        console.log("WebSocket closed, retrying...");
        setTimeout(initWebSocket, 2000);
    };

    websocket.onmessage = function (event) {
        let data;
        try {
            data = JSON.parse(event.data);
        } catch (e) {
            console.warn("ws: invalid json", e);
            return;
        }

        // Legacy: full payload { valves, bus0, bus1 }
        if (data && data.valves && (data.bus0 || data.bus1)) {
            data.valves.forEach((pos, idx) => {
                let cell = document.getElementById("valve" + idx);
                if (cell) cell.textContent = pos;
            });
            if (data.bus0) {
                data.bus0.forEach((s, idx) => {
                    const prefix = "bus0_sensor" + idx + "_";
                    updateSensorDOM(prefix, s);
                });
            }
            if (data.bus1) {
                data.bus1.forEach((s, idx) => {
                    const prefix = "bus1_sensor" + idx + "_";
                    updateSensorDOM(prefix, s);
                });
            }
            return;
        }

        // New: per-sensor object { bus, ch, ... }
        if (typeof data.bus === 'number' && typeof data.ch === 'number') {
            const prefix = "bus" + data.bus + "_sensor" + data.ch + "_";
            updateSensorDOM(prefix, data);
            return;
        }

        // New: valves-only message { valves: [...] }
        if (data && data.valves) {
            data.valves.forEach((pos, idx) => {
                let cell = document.getElementById("valve" + idx);
                if (cell) cell.textContent = pos;
            });
            return;
        }

        // Unknown message shape; ignore
    };

    // Helper to update DOM fields for a single sensor (keeps code DRY)
    function updateSensorDOM(prefix, s) {
        const setText = (idSuffix, val) => {
            const el = document.getElementById(prefix + idSuffix);
            if (el) el.textContent = (val !== undefined && val !== null) ? val : '';
        };
        setText("type", s.type);
        setText("temp", s.temp);
        setText("hum", s.rh);
        setText("co2", s.co2);
        setText("valve", s.valve);
        setText("location", s.location);
        setText("rhs", s.rhs);
        setText("co2s", s.co2s);
    }
}

function request_index_data() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("get_index_data");
    }
}

window.addEventListener('load', initWebSocket);