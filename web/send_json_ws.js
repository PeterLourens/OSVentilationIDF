// send_json_ws.js
// Complete WebSocket client for OSVentilation with debug logging and statemachine handling.

(function () {
    'use strict';

    // WebSocket endpoint (uses same host and port as page)
    const GATEWAY = 'ws://' + window.location.host + '/ws';
    const RECONNECT_MS = 2000;
    const REFRESH_MS = 60000; // 60 seconds

    let websocket = null;
    let lastSeq = 0;
    let currentSeq = 0;
    let pollIntervalId = null;

    // Single-tab leader election to avoid multiple tabs polling
    const bc = ('BroadcastChannel' in window) ? new BroadcastChannel('osv_stats_channel') : null;
    let isLeader = true;

    function announceLeader() {
        if (!bc) return;
        bc.postMessage({ type: 'hello', ts: Date.now() });
    }

    if (bc) {
        bc.onmessage = (ev) => {
            const msg = ev.data || {};
            if (msg.type === 'hello') {
                if (isLeader) bc.postMessage({ type: 'leader', ts: Date.now() });
            } else if (msg.type === 'leader') {
                isLeader = false;
            } else if (msg.type === 'i_am_closing') {
                isLeader = true;
            }
        };

        announceLeader();
        setTimeout(() => { if (isLeader) announceLeader(); }, 200);
        window.addEventListener('beforeunload', () => { if (bc) bc.postMessage({ type: 'i_am_closing' }); });
    }

    // Map visible page names to server command tokens
    const PAGE_TO_CMD = {
        'stats': 'stats',
        'status': 'stats',
        'index': 'index',
        'sensor_config': 'sensor_config',
        'sensor-config': 'sensor_config',
        'sensorconfig': 'sensor_config',
        'configuration': 'config',
        'config': 'config',
        'settings': 'config',
        'statemachine': 'statemachine',
        'valvecontrol': 'valvecontrol',
        'webserial': 'webserial'
    };

    // Statemachine cache for assembling chunked messages
    const statemachineCache = {
        totalStates: 0,
        received: {},
        order: []
    };

    // Simple dedupe store for statemachine messages
    if (!window._sm_dedupe) window._sm_dedupe = {};

    function initWebSocket() {
        try {
            websocket = new WebSocket(GATEWAY);
        } catch (e) {
            console.error('WS init failed', e);
            setTimeout(initWebSocket, RECONNECT_MS);
            return;
        }

        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
        websocket.onerror = onError;
    }

    window.addEventListener('load', initWebSocket);

    function onOpen() {
        console.info('WS open', GATEWAY);
        if (isLeader) requestPageData();

        if (!pollIntervalId) {
            pollIntervalId = setInterval(() => {
                if (isLeader) requestPageData();
            }, REFRESH_MS);
        }
    }

    function onClose() {
        console.warn('WS closed, reconnecting in', RECONNECT_MS, 'ms');
        if (pollIntervalId) {
            clearInterval(pollIntervalId);
            pollIntervalId = null;
        }
        setTimeout(initWebSocket, RECONNECT_MS);
    }

    function onError(e) {
        console.error('WS error', e);
    }

    // Send the page name mapped to server command tokens
    function requestPageData() {
        const page = document.getElementById('page_name');
        if (!page || !websocket || websocket.readyState !== WebSocket.OPEN) return;

        const raw = (page.textContent || page.innerText || page.innerHTML || '').trim();
        if (!raw) return;

        const key = raw.toLowerCase();
        const cmd = PAGE_TO_CMD[key] || key;

        try {
            websocket.send(cmd);
            console.info('WS sent command:', cmd, '(page:', raw, ')');
        } catch (e) {
            console.error('WS send failed for command', cmd, e);
        }
    }

    // Main incoming message handler with debug logging
    function onMessage(evt) {
        try { console.debug('WS raw:', evt.data); } catch (e) { /* ignore logging errors */ }

        const raw = evt.data;
        let obj;
        try {
            obj = JSON.parse(raw);
        } catch (e) {
            console.debug('WS: non-JSON message received:', raw);
            return;
        }

        try { console.debug('WS parsed:', obj); } catch (e) { /* ignore logging errors */ }

        const type = obj.type || null;
        const seq = Number.isFinite(obj.seq) ? obj.seq : 0;

        if (seq && seq < lastSeq) {
            console.debug('WS: ignoring stale seq', seq, 'lastSeq', lastSeq);
            return;
        }
        if (seq) lastSeq = seq;

        if (type && type.startsWith('statemachine')) {
            handleStatemachineMessage(obj);
            return;
        }

        if (type === 'summary') {
            handleSummary(obj);
            return;
        }

        if (type === 'tasks_start') {
            currentSeq = obj.seq || 0;
            clearTasksTable();
            if (obj.total !== undefined) {
                const totalEl = document.getElementById('tasks_total');
                if (totalEl) totalEl.textContent = obj.total;
            }
            return;
        }

        if (type === 'tasks_page') {
            if (obj.seq && currentSeq && obj.seq !== currentSeq) {
                console.debug('WS: tasks_page seq mismatch', obj.seq, 'currentSeq', currentSeq);
                return;
            }
            if (Array.isArray(obj.tasks)) {
                appendTasksRows(obj.tasks);
            }
            return;
        }

        if (type === 'tasks_end') {
            console.debug('WS: tasks_end received seq', obj.seq);
            return;
        }

        if (type === 'settings' || type === 'config' || type === 'configuration') {
            handleSettings(obj);
            return;
        }

        applyPayloadToDOM(obj);
    }

    // Summary handler: update small status spans
    function handleSummary(obj) {
        if (obj.time !== undefined) {
            const el = document.getElementById('stat_time');
            if (el) el.textContent = obj.time;
        }
        if (obj.uptime_s !== undefined) {
            const el = document.getElementById('stat_uptime');
            if (el) {
                const s = Number(obj.uptime_s) || 0;
                const days = Math.floor(s / 86400);
                const hours = Math.floor((s % 86400) / 3600);
                const mins = Math.floor((s % 3600) / 60);
                const secs = s % 60;
                let human = '';
                if (days) human += days + 'd ';
                human += String(hours).padStart(2, '0') + ':' + String(mins).padStart(2, '0') + ':' + String(secs).padStart(2, '0');
                el.textContent = human;
            }
        }
        if (obj.cpu_temp_c !== undefined) {
            const el = document.getElementById('stat_cpu_temp');
            if (el) el.textContent = (typeof obj.cpu_temp_c === 'number') ? obj.cpu_temp_c.toFixed(1) + ' °C' : obj.cpu_temp_c;
        }
        if (obj.heap_free !== undefined) {
            const el = document.getElementById('stat_heap_free');
            if (el) el.textContent = obj.heap_free;
        }
        if (obj.stat_sm_state !== undefined) {
            const el = document.getElementById('stat_sm_state');
            if (el) el.textContent = obj.stat_sm_state;
        }
        if (obj.stat_wifi_ip !== undefined) {
            const el = document.getElementById('stat_wifi_ip');
            if (el) el.textContent = obj.stat_wifi_ip;
        }
        if (obj.stat_eth_ip !== undefined) {
            const el = document.getElementById('stat_eth_ip');
            if (el) el.textContent = obj.stat_eth_ip;
        }
    }

    // Settings/config handler: map known fields to DOM
    function handleSettings(obj) {
        if (obj.device_name !== undefined) {
            const el = document.getElementById('cfg_device_name');
            if (el) el.textContent = obj.device_name;
        }
        if (obj.wifi_ssid !== undefined) {
            const el = document.getElementById('cfg_wifi_ssid');
            if (el) el.textContent = obj.wifi_ssid;
        }
        if (obj.mqtt_enabled !== undefined) {
            const el = document.getElementById('cfg_mqtt_enabled');
            if (el) el.textContent = obj.mqtt_enabled ? 'on' : 'off';
        }
        applyPayloadToDOM(obj);
    }

    // Statemachine message handling
    function handleStatemachineMessage(obj) {
        const type = obj.type || '';

        if (type === 'statemachine_start') {
            statemachineCache.totalStates = Number(obj.total_states || obj.total || 0) || 0;
            statemachineCache.received = {};
            statemachineCache.order = [];
            const container = document.getElementById('statemachine_container');
            if (container) container.innerHTML = '';
            console.info('Statemachine start, expecting', statemachineCache.totalStates, 'states');
            return;
        }

        if (type === 'statemachine_general') {
            applyPayloadToDOM(obj);
            console.debug('Statemachine general received', obj);
            return;
        }

        if (type === 'statemachine_state') {
            const idx = (typeof obj.state_index !== 'undefined') ? Number(obj.state_index) : null;
            if (idx === null) {
                console.warn('statemachine_state without state_index', obj);
                return;
            }

            // dedupe: skip identical messages for same index arriving quickly
            const dedupeKey = 'sm_' + idx;
            const payloadStr = JSON.stringify(obj);
            const prev = window._sm_dedupe[dedupeKey];
            if (prev && prev.str === payloadStr && (Date.now() - prev.ts) < 1000) {
                console.debug('Duplicate statemachine_state ignored for index', idx);
                return;
            }
            window._sm_dedupe[dedupeKey] = { str: payloadStr, ts: Date.now() };

            statemachineCache.received[idx] = obj;
            statemachineCache.order.push(idx);

            renderStatemachineState(idx, obj);
            console.debug('Statemachine state received index', idx, obj);
            return;
        }

        if (type === 'statemachine_end') {
            console.info('Statemachine end received. States received:', Object.keys(statemachineCache.received).length);
            finalizeStatemachineUI();
            return;
        }

        console.debug('Unhandled statemachine message type', type);
    }

    // Render a single state object into the UI, filling form placeholders if present
    function renderStatemachineState(index, stateObj) {
        try {
            console.debug('renderStatemachineState: filling state', index, stateObj);

            // Helper to safely set element text/value and log it
            function setElementValue(id, value) {
                const el = document.getElementById(id);
                if (!el) {
                    return false;
                }
                if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT') {
                    el.value = (value === null || typeof value === 'undefined') ? '' : String(value);
                } else {
                    el.textContent = (value === null || typeof value === 'undefined') ? '' : String(value);
                }
                console.debug('renderStatemachineState: set', id, '=>', value);
                return true;
            }

            // s_en: boolean -> "On"/"Off" select
            if (typeof stateObj.s_en !== 'undefined') {
                const enVal = stateObj.s_en ? 'On' : 'Off';
                setElementValue('s' + index + '_en', enVal);
            }

            // fanspeed
            if (typeof stateObj.s_fanspeed !== 'undefined') {
                setElementValue('s' + index + '_fanspeed', stateObj.s_fanspeed);
            }

            // name
            if (typeof stateObj.s_name !== 'undefined') {
                setElementValue('s' + index + '_name', stateObj.s_name);
            }

            // numeric fields mapping
            const numericFields = {
                s_st_h: 's' + index + '_st_h',
                s_st_m: 's' + index + '_st_m',
                s_end_h: 's' + index + '_end_h',
                s_end_m: 's' + index + '_end_m',
                s_dur_m: 's' + index + '_dur_m',
                s_cycl_int_m: 's' + index + '_cycl_int_m'
            };
            for (const key in numericFields) {
                if (!Object.prototype.hasOwnProperty.call(numericFields, key)) continue;
                if (typeof stateObj[key] !== 'undefined') {
                    setElementValue(numericFields[key], stateObj[key]);
                }
            }

            // valves: s_valves -> s{index}_v{j}_pos
            if (Array.isArray(stateObj.s_valves)) {
                stateObj.s_valves.forEach((v, j) => {
                    const vid = 's' + index + '_v' + j + '_pos';
                    setElementValue(vid, v);
                });
            }

            // If no placeholders exist, create/update a dynamic block as fallback
            const anyPlaceholder = !!document.getElementById('s' + index + '_name') ||
                !!document.getElementById('s' + index + '_fanspeed') ||
                !!document.getElementById('s' + index + '_v0_pos');

            if (!anyPlaceholder) {
                setTimeout(() => {
                    let container = document.getElementById('statemachine_container');
                    if (!container) {
                        container = document.createElement('div');
                        container.id = 'statemachine_container';
                        const main = document.getElementById('main') || document.body;
                        main.appendChild(container);
                        console.debug('renderStatemachineState: created statemachine_container');
                    }

                    const rowId = 'sm_state_' + index;
                    let row = document.getElementById(rowId);
                    if (!row) {
                        row = document.createElement('div');
                        row.id = rowId;
                        row.className = 'sm-state-row';
                        row.innerHTML = `<h4 class="sm-state-title">State ${index}</h4>
                             <div class="sm-state-body"></div>`;
                        container.appendChild(row);
                        console.debug('renderStatemachineState: created dynamic row', rowId);
                    }

                    const body = row.querySelector('.sm-state-body');
                    if (!body) return;

                    const name = stateObj.s_name || '';
                    const fanspeed = stateObj.s_fanspeed || '';
                    const startH = (typeof stateObj.s_st_h !== 'undefined') ? stateObj.s_st_h : '';
                    const startM = (typeof stateObj.s_st_m !== 'undefined') ? stateObj.s_st_m : '';
                    const endH = (typeof stateObj.s_end_h !== 'undefined') ? stateObj.s_end_h : '';
                    const endM = (typeof stateObj.s_end_m !== 'undefined') ? stateObj.s_end_m : '';
                    const dur = (typeof stateObj.s_dur_m !== 'undefined') ? stateObj.s_dur_m : '';
                    const cycl = (typeof stateObj.s_cycl_int_m !== 'undefined') ? stateObj.s_cycl_int_m : '';
                    const valves = Array.isArray(stateObj.s_valves) ? stateObj.s_valves.join(', ') : '';

                    body.innerHTML = '';
                    const fields = [
                        { label: 'Name', value: name },
                        { label: 'Fanspeed', value: fanspeed },
                        { label: 'Start', value: `${startH}:${String(startM).padStart(2, '0')}` },
                        { label: 'End', value: `${endH}:${String(endM).padStart(2, '0')}` },
                        { label: 'Duration (min)', value: dur },
                        { label: 'Cycle interval (min)', value: cycl },
                        { label: 'Valves', value: valves }
                    ];

                    fields.forEach(f => {
                        const p = document.createElement('div');
                        p.className = 'sm-field';
                        const lbl = document.createElement('strong');
                        lbl.textContent = f.label + ': ';
                        const val = document.createElement('span');
                        val.textContent = (f.value === null || typeof f.value === 'undefined') ? '' : String(f.value);
                        p.appendChild(lbl);
                        p.appendChild(val);
                        body.appendChild(p);
                    });

                    console.debug('renderStatemachineState: rendered dynamic block for', rowId);
                }, 0);
            }
        } catch (err) {
            console.error('renderStatemachineState: unexpected error', err);
        }
    }

    // Finalize UI after all states arrived
    function finalizeStatemachineUI() {
        const total = statemachineCache.totalStates || 0;
        if (total > 0) {
            const missing = [];
            for (let i = 0; i < total; ++i) {
                if (!Object.prototype.hasOwnProperty.call(statemachineCache.received, i)) missing.push(i);
            }
            if (missing.length) {
                console.warn('Statemachine missing states:', missing);
            } else {
                console.info('All statemachine states received');
            }
        }
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

    // Generic helper: map payload keys to DOM elements by id
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

    // Expose small API for debugging
    window.OSV_WS = {
        requestPageData: requestPageData,
        getLastSeq: () => lastSeq,
        isLeader: () => isLeader
    };

})();
