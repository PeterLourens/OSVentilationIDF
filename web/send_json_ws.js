// send_json_ws.js - client for paged tasks (tasks_start/tasks_page/tasks_end)
(function () {
    'use strict';

    const GATEWAY = 'ws://' + window.location.hostname + ':/ws';
    const RECONNECT_MS = 2000;
    const REFRESH_MS = 30000;

    let websocket = null;
    let lastSeq = 0;
    let currentSeq = 0;

    function initWebSocket() {
        websocket = new WebSocket(GATEWAY);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
        websocket.onerror = onError;
    }

    window.addEventListener('load', initWebSocket);

    function onOpen() {
        requestPageData();
        const page = document.getElementById('page_name');
        if (!page) return;
        const name = page.textContent || page.innerHTML;
        if (name === 'stats') setInterval(requestPageData, REFRESH_MS);
    }

    function onClose() { setTimeout(initWebSocket, RECONNECT_MS); }
    function onError(e) { console.debug('WS error', e); }

    function requestPageData() {
        const page = document.getElementById('page_name');
        if (!page || !websocket || websocket.readyState !== WebSocket.OPEN) return;
        websocket.send(page.textContent || page.innerHTML);
    }

    function onMessage(evt) {
        const raw = evt.data;
        let obj;
        try {
            obj = JSON.parse(raw);
        } catch (e) {
            // ignore non-JSON messages
            return;
        }

        const type = obj.type || null;
        const seq = Number.isFinite(obj.seq) ? obj.seq : 0;

        // ignore stale sequences
        if (seq && seq < lastSeq) return;
        if (seq) lastSeq = seq;

        if (type === 'tasks_start') {
            currentSeq = obj.seq || 0;
            clearTasksTable();
            // optional: show total somewhere
            if (obj.total !== undefined) {
                const totalEl = document.getElementById('tasks_total');
                if (totalEl) totalEl.textContent = obj.total;
            }
            return;
        }

        if (type === 'tasks_page') {
            // only accept pages for currentSeq
            if (obj.seq && currentSeq && obj.seq !== currentSeq) {
                // ignore pages from previous runs
                return;
            }
            // append rows
            if (Array.isArray(obj.tasks)) {
                appendTasksRows(obj.tasks);
            }
            return;
        }

        if (type === 'tasks_end') {
            // optionally finalize UI (e.g., enable controls)
            return;
        }

        // fallback: map payload keys to DOM elements by id
        applyPayloadToDOM(obj);
    }

    function clearTasksTable() {
        const tbody = document.getElementById('stat_tasks_full_body');
        if (tbody) tbody.innerHTML = '';
    }

    function appendTasksRows(tasks) {
        const tbody = document.getElementById('stat_tasks_full_body');
        if (!tbody) return;
        tasks.forEach(t => {
            const tr = document.createElement('tr');

            const tdName = document.createElement('td'); tdName.textContent = t.name || ''; tr.appendChild(tdName);
            const tdState = document.createElement('td'); tdState.textContent = t.state || ''; tr.appendChild(tdState);
            const tdPrio = document.createElement('td'); tdPrio.textContent = t.prio !== undefined ? t.prio : ''; tr.appendChild(tdPrio);
            const tdRun = document.createElement('td'); tdRun.textContent = t.run_ticks !== undefined ? t.run_ticks : ''; tr.appendChild(tdRun);
            const tdCpu = document.createElement('td'); tdCpu.textContent = t.cpu_pct !== undefined ? Number(t.cpu_pct).toFixed(2) : ''; tr.appendChild(tdCpu);
            const tdStack = document.createElement('td'); tdStack.textContent = t.stack_high_water !== undefined ? t.stack_high_water : ''; tr.appendChild(tdStack);

            tbody.appendChild(tr);
        });
    }

    function applyPayloadToDOM(payload) {
        if (!payload || typeof payload !== 'object') return;
        for (const key in payload) {
            if (!Object.prototype.hasOwnProperty.call(payload, key)) continue;
            const el = document.getElementById(key);
            if (!el) continue;
            const val = payload[key];
            if (el.tagName === 'INPUT' && el.type === 'checkbox') el.checked = Boolean(val);
            else if (el.tagName === 'SELECT') {
                el.value = val;
                if (!Array.from(el.options).some(o => o.value === el.value)) {
                    for (let o of el.options) if (o.text === val) { o.selected = true; break; }
                }
            } else if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA') el.value = val;
            else el.textContent = val;
        }
    }

    // expose small API
    window.OSV_WS = { requestPageData: requestPageData, getLastSeq: () => lastSeq };

})();
