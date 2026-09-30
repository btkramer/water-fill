//TAB - HTML

String getHTML() {
    String html = R"RAW(
<!DOCTYPE html>
<html>
<head>
    <title>Water Fill</title>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { 
            font-family: 'Roboto', sans-serif; 
            background: #f5f5f5; 
            color: #333; 
            line-height: 1.4; 
            margin: 0; 
            padding: 20px; 
            box-sizing: border-box; 
        }
        @font-face { 
            font-family: 'Roboto'; 
            src: url('https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=swap'); 
        }
        .container { 
            max-width: 700px; 
            margin: auto; 
            padding: 10px; 
            background: white; 
            border-radius: 10px; 
            box-shadow: 0 0 10px rgba(0,0,0,0.1); 
        }
        .debug-button {
            background: #607B8B;
            border: none;
            color: white;
            padding: 5px 10px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 0.8em;
            margin: 4px 2px;
            cursor: pointer;
            border-radius: 5px;
        }
        .gear-icon {
            position: absolute;
            top: 0px;
            right: 10px;
            cursor: pointer;
            font-size: 3.5em;
            text-decoration: none;
        }
        .gear-icon::before {
            content: '\2699'; // Unicode for gear symbol
            color: #2196F3; // Blue color, you can change this
            font-family: 'Roboto'; // Ensure the font supports this symbol
        }
        .header { 
            text-align: center; 
            color: #2196F3; 
            font-size: 3.5em; 
            font-weight: bold; 
            margin-bottom: 10px; 
        }
        .time-info { 
            text-align: center; 
            font-size: 2.5em; 
            margin: 5px 0 2px 0; 
            color: black; 
        }
        .date-info { 
            text-align: center; 
            font-size: 1.2em; 
            margin: 0 0 5px 0; 
            color: black; 
        }
        .auto-start { 
            text-align: center; 
            font-size: 1.2em; 
            margin: 10px 0; 
        }
        .control-section, .mode-section { 
            display: flex; 
            flex-direction: column; 
            align-items: center; 
            margin: 10px 0; 
        }
        .slider-group { 
            display: flex; 
            justify-content: center; 
            align-items: center; 
            margin-bottom: 0px; 
            margin-top: 20px; 
            width: 100%; 
        }
        .increment-button { 
            background: #2196F3; 
            border: none; 
            color: white; 
            padding: 10px 15px; 
            margin: 0 5px; 
            cursor: pointer; 
            border-radius: 5px; 
            font-size: 16px; 
        }
        .slider { 
            width: 100%; 
        }
        .mode-section { 
            display: flex; 
            flex-direction: row; 
            align-items: center; 
            justify-content: center; 
            margin: 10px 0; 
            width: 100%; 
        }
        .mode-label { 
            margin-right: 10px; 
            white-space: nowrap;
            font-size: 1.5em;
        }
        .toggle-button { 
            padding: 10px 15px; 
            cursor: pointer; 
            color: white; 
            border: none; 
            border-radius: 5px; 
            background-color: transparent; 
            font-size: 16px; 
        }
        .toggle-button.disabled { 
            background: #ccc; 
            cursor: not-allowed; 
        }
        .toggle-button[data-mode='Auto'] { 
            background-color: lightgreen !important; 
        }
        .toggle-button[data-mode='Manual'] { 
            background-color: lightcoral !important; 
        }
        .devices-container { 
            display: grid; 
            grid-template-columns: 1fr; 
            gap: 15px; 
        }
        .device { 
            display: flex; 
            justify-content: space-between; 
            align-items: center; 
            background: #f9f9f9; 
            padding: 10px; 
            border-radius: 5px; 
            box-shadow: 0 1px 3px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.24); 
        }
        .label { 
            flex: 1;
            font-size: 1.2em;
        }
        .circle-container { 
            width: 30px; 
            height: 30px; 
            margin: 0 10px; 
        }
        .circle { 
            width: 100%; 
            height: 100%; 
            border-radius: 50%; 
            transition: background-color 0.3s; 
        }
        .circle.off { 
            background-color: #4CAF50; // Green for off state
        }
        .circle.on { 
            background-color: #F44336; // Red for on state
        }
        .low-indicator {
            color: red;
            margin-right: 5px;
            display: inline; /* Always inline for devices */
        }
        .device .label .low-indicator {
            margin-left: 10px;
        }
        .tank-low-indicator {
            color: red; 
        }
        .control-section .label .low-indicator {
            margin-left: 0; 
        }
        .enable-button { 
            padding: 10px 15px; 
            border: none; 
            color: white; 
            cursor: pointer; 
            border-radius: 5px; 
            font-size: 16px; 
        }
        .enable-button.enabled { 
            background-color: #4CAF50; 
        }
        .enable-button.disabled { 
            background-color: #F44336; 
        }
        .auto-start button { 
            background: #2196F3; 
            border: none; 
            color: white; 
            padding: 10px 15px; 
            cursor: pointer; 
            border-radius: 5px; 
            margin-left: 10px; 
            font-size: 16px; 
        }
        .toggle-button[data-label='Stop'] { 
            background-color: #80d8ff; 
        }
        .toggle-button:not(.disabled):not([data-label='Stop']) { 
            background-color: #2196F3; 
        }
        .toggle-button.disabled { 
            background-color: #ccc; 
        }
        .control-section p { 
            margin: 0 0 0 0; 
        }
        .control-section { 
            display: flex; 
            flex-direction: column; 
            align-items: center; 
            margin: 10px 0 30px; 
            font-size: 1.2em;
        }
        @media screen and (max-width: 600px) {
            .header { 
                font-size: 2em; 
            }
            .time-info { 
                font-size: 1.5em; 
            }
            .date-info { 
                font-size: 1em; 
            }
            .devices-container { 
                grid-template-columns: 1fr; 
            }
        }
    </style>
</head>
<body>
    <div class='container' style='position: relative;'>
        <a href='/settings' class='gear-icon'></a>
        <div class='header'>Water Fill</div>
        <div class='time-info' id='time'></div>
        <div class='date-info' id='date'></div>
        <div class='auto-start'>Fill Time: <span id='autoStartTime'>)RAW" +
                  String((timeData.autoStart.Hour % 12 == 0 ? 12 : timeData.autoStart.Hour % 12)) + ":" +
                  (timeData.autoStart.Minute < 10 ? "0" + String(timeData.autoStart.Minute) : String(timeData.autoStart.Minute)) + " " +
                  (timeData.autoStart.Hour < 12 ? "AM" : "PM") + R"RAW(</span> <button onclick='showTimePicker()'>Change</button></div>
        <div style='display:none;' id='AutoStartHour'>)RAW" + String(timeData.autoStart.Hour) + R"RAW(</div>
        <div style='display:none;' id='AutoStartMinute'>)RAW" + String(timeData.autoStart.Minute) + R"RAW(</div>
        <div class='control-section'>
            <div class='slider-group'>
                <button class='increment-button' onclick='adjustMaxPumpTime(-1)'>-</button>
                <input type='range' min='0' max='300' value=')RAW" + String(timeData.setpoint.MaxPumpRun) + R"RAW(' class='slider' id='pumpSlider' onchange='updateMaxPumpTime(this.value)'>
                <button class='increment-button' onclick='adjustMaxPumpTime(1)'>+</button>
            </div>
            <p style='text-align: center;'>Pump Max Run Time(sec): <span id='pumpTime'>)RAW" + String(timeData.setpoint.MaxPumpRun) + R"RAW(</span></p>
        </div>
        <div class='mode-section'>
            <span class='mode-label'>Mode:</span>
            <button class='toggle-button' id='modeButton' data-mode=')RAW" + String(var.mode ? "Auto" : "Manual") + R"RAW(' onclick='toggleMode()'>)RAW" + String(var.mode ? "Auto" : "Manual") + R"RAW(</button>
        </div>
        <div class='control-section'>
            <span id='tankLevelIndicator' style='display: none;'><div class='label'>Tank Level: <span class='tank-low-indicator'>Water Low</span></div></span>
        </div>
        <div class='devices-container'>
    )RAW";
    for (int i = 0; i < 4; i++) {
        bool onState = (i == 0) ? alias.DO.CPAP_R1 : 
                       (i == 1) ? alias.DO.BrianDP_R2 : 
                       (i == 2) ? alias.DO.DinaDP_R3 : 
                                  alias.DO.Ozone_R4;
        String lowIndicator = (i < 3) ? " <span id='lowIndicator" + String(alias.deviceTags[i]) + "' class='low-indicator'>Water Low</span>" : ""; // Exclude Ozone (i=3)
        html += String("<div class='device'>") +
                String("<div class='label'>") + String(alias.deviceNames[i]) + lowIndicator + "</div>" +
                String("<button class='enable-button ") + String(*enabled.deviceEnableStates[i] ? "enabled" : "disabled") + "' id='enableButton" + String(alias.deviceTags[i]) + "' onclick='toggleEnable(\"" + String(alias.deviceTags[i]) + "\")'>" + String(*enabled.deviceEnableStates[i] ? "Enabled" : "Disabled") + "</button>" +
                String("<div class='circle-container'><div class='circle ") + String(onState ? "on" : "off") + "' id='circle" + String(alias.deviceTags[i]) + "'></div></div>" +
                String("<button class='toggle-button ") + String(var.mode || !(*enabled.deviceEnableStates[i]) ? "disabled" : "") + "' id='button" + String(alias.deviceTags[i]) + "' data-label='" + String(onState ? "Start" : "Stop") + "' onclick='toggleDevice(\"" + String(alias.deviceTags[i]) + "\")'>" + String(onState ? "Start" : "Stop") + "</button>" +
                String("</div>");
    }
    html += R"RAW(
        <div class='control-section'>
            <div class='slider-group'>
                <button class='increment-button' onclick='adjustOzoneRunTime(-1)'>-</button>
                <input type='range' min='0' max='300' value=')RAW" + String(timeData.setpoint.OzoneRun) + R"RAW(' class='slider' id='ozoneSlider' onchange='updateOzoneRunTime(this.value)'>
                <button class='increment-button' onclick='adjustOzoneRunTime(1)'>+</button>
            </div>
            <p style='text-align: center;'>Ozone Run Time(sec): <span id='ozoneRunTime'>)RAW" + String(timeData.setpoint.OzoneRun) + R"RAW(</span></p>
        </div>
        <div id='timePickerOverlay' style='display:none; position:fixed; top:0; left:0; width:100%; height:100%; background-color:rgba(0,0,0,0.5); z-index:100;'>
            <div style='position:absolute; top:50%; left:50%; transform:translate(-50%, -50%); background:white; padding:20px; border-radius:10px;'>
                <h2>Set Auto Start Time</h2>
                <input type='time' id='timeInput' step='60' onchange='updateAutoStartTime()'>
                <button onclick='closeTimePicker()'>Cancel</button>
            </div>
        </div>
        <script>
            function updateLowIndicators(states) {
                for (let tag in states) {
                    if (tag === 'Tank') {
                        let tankIndicator = document.getElementById('tankLevelIndicator');
                        if (tankIndicator) {
                            tankIndicator.style.display = states[tag] ? 'none' : 'inline';
                        }
                    } else {
                        let indicator = document.getElementById('lowIndicator' + tag);
                        if (indicator) {
                            indicator.style.display = states[tag] ? 'none' : 'inline';
                        }
                    }
                }
            }

            function updatePumpTime(value) {
                document.getElementById('pumpTime').innerText = value;
            }

            function toggleDevice(device) {
                if(document.getElementById('modeButton').textContent !== 'Auto' && document.getElementById('enableButton' + device).classList.contains('enabled')) {
                    var button = document.getElementById('button' + device);
                    button.disabled = true;
                    var xhr = new XMLHttpRequest();
                    xhr.onreadystatechange = function() {
                        if (this.readyState == 4 && this.status == 200) {
                            var response = this.responseText.split(':');
                            var circle = document.getElementById('circle' + response[0]);
                            circle.className = 'circle ' + response[1];
                            button.textContent = response[1] === 'on' ? 'Start' : 'Stop';
                            button.setAttribute('data-label', response[1] === 'on' ? 'Start' : 'Stop');
                            setTimeout(() => button.disabled = false, 500);
                        }
                    };
                    xhr.open('POST', '/toggle', true);
                    xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
                    xhr.send('device=' + device);
                }
            }

            function toggleMode() {
                var xhr = new XMLHttpRequest();
                xhr.onreadystatechange = function() {
                    if (this.readyState == 4 && this.status == 200) {
                        var modeButton = document.getElementById('modeButton');
                        modeButton.textContent = this.responseText;
                        modeButton.setAttribute('data-mode', this.responseText);
                        var buttons = document.querySelectorAll('.toggle-button');
                        buttons.forEach(button => {
                            if (button.id !== 'modeButton') {
                                var deviceTag = button.id.slice(6);
                                var enableButton = document.getElementById('enableButton' + deviceTag);
                                button.classList.toggle('disabled', this.responseText === 'Auto' || !enableButton.classList.contains('enabled'));
                            }
                        });
                    }
                };
                xhr.open('POST', '/toggleMode', true);
                xhr.send();
            }

            function updateOzoneRunTime(value) {
                document.getElementById('ozoneRunTime').innerText = value;
                var xhr = new XMLHttpRequest();
                xhr.open('POST', '/updateOzoneRunTime', true);
                xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
                xhr.send('value=' + value);
                console.log('Ozone Run Time updated to:', value);
            }

            function adjustOzoneRunTime(change) {
                var currentValue = parseInt(document.getElementById('ozoneRunTime').innerText);
                var newValue = Math.max(0, Math.min(300, currentValue + change));
                document.getElementById('ozoneSlider').value = newValue;
                updateOzoneRunTime(newValue);
            }

            function updateMaxPumpTime(value) {
                document.getElementById('pumpTime').innerText = value;
                var xhr = new XMLHttpRequest();
                xhr.open('POST', '/updateMaxPumpTime', true);
                xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
                xhr.send('value=' + value);
            }

            function adjustMaxPumpTime(change) {
                var currentValue = parseInt(document.getElementById('pumpTime').innerText);
                var newValue = Math.max(0, Math.min(500, currentValue + change));
                document.getElementById('pumpSlider').value = newValue;
                updateMaxPumpTime(newValue);
            }

            function toggleEnable(device) {
                var xhr = new XMLHttpRequest();
                xhr.onreadystatechange = function() {
                    if (this.readyState == 4 && this.status == 200) {
                        var response = this.responseText.split(':');
                        var enableButton = document.getElementById('enableButton' + response[0]);
                        enableButton.textContent = response[1] === 'enabled' ? 'Enabled' : 'Disabled';
                        enableButton.className = 'enable-button ' + response[1];
                        var startStopButton = document.getElementById('button' + response[0]);
                        var circle = document.getElementById('circle' + response[0]);
                        if (response[1] === 'disabled') {
                            startStopButton.textContent = 'Start';
                            startStopButton.setAttribute('data-label', 'Start');
                            circle.className = 'circle on'; // Red (OFF)
                        } else {
                            startStopButton.textContent = 'Start';
                            startStopButton.setAttribute('data-label', 'Start');
                            circle.className = 'circle on'; // Red (OFF)
                        }
                        startStopButton.classList.toggle('disabled', response[1] === 'disabled' || document.getElementById('modeButton').textContent === 'Auto');
                    }
                };
                xhr.open('POST', '/toggleEnable', true);
                xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
                xhr.send('device=' + device);
            }

            //Update Time
            function updateTime() {
                var xhr = new XMLHttpRequest();
                xhr.onreadystatechange = function() {
                    if (this.readyState == 4 && this.status == 200) {
                        var data = JSON.parse(this.responseText);
                        document.getElementById('time').innerHTML = data.time || 'Time not set';
                        document.getElementById('date').innerHTML = data.date || 'Date not set';
                        var devices = data.devices;
                        for (var device in devices) {
                            var circle = document.getElementById('circle' + device);
                            var button = document.getElementById('button' + device);
                            if (circle && button) {
                                circle.className = 'circle ' + (devices[device] ? 'off' : 'on');
                                button.textContent = devices[device] ? 'Stop' : 'Start';
                                button.setAttribute('data-label', devices[device] ? 'Stop' : 'Start');
                            }
                        }
                        updateLowIndicators(data.levels);
                    }
                };
                xhr.open('GET', '/time', true);
                xhr.send();
            }

            function showTimePicker() {
                var overlay = document.getElementById('timePickerOverlay');
                var input = document.getElementById('timeInput');
                var autoStartHour = document.getElementById('AutoStartHour').innerHTML;
                var autoStartMinute = document.getElementById('AutoStartMinute').innerHTML;
                input.value = autoStartHour.padStart(2, '0') + ':' + autoStartMinute.padStart(2, '0');
                overlay.style.display = 'block';
                input.focus();
            }

            function updateAutoStartTime() {
                var time = document.getElementById('timeInput').value;
                var parts = time.split(':');
                var newHour = parseInt(parts[0]);
                var newMinute = parseInt(parts[1]);
                if (newHour >= 0 && newHour < 24 && newMinute >= 0 && newMinute < 60) {
                    var xhr = new XMLHttpRequest();
                    xhr.open('POST', '/updateAutoStartTime', true);
                    xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
                    xhr.send('hour=' + newHour + '&minute=' + newMinute);
                    document.getElementById('autoStartTime').innerHTML = (newHour % 12 == 0 ? 12 : newHour % 12) + ':' + (newMinute < 10 ? '0' + newMinute : newMinute) + ' ' + (newHour < 12 ? 'AM' : 'PM');
                    document.getElementById('AutoStartHour').innerHTML = newHour;
                    document.getElementById('AutoStartMinute').innerHTML = newMinute;
                    closeTimePicker();
                } else {
                    alert('Invalid time entered.');
                }
            }

            function closeTimePicker() {
                document.getElementById('timePickerOverlay').style.display = 'none';
            }

            window.onload = function() {
                updateTime();
                setInterval(updateTime, 500);
            };
        </script>
    </div>
</body>
</html>
    )RAW";
    return html;
}


//Settings Page
String getSettingsHTML() {
    String html = R"RAW(
<!DOCTYPE html>
<html>
<head>
    <title>Settings</title>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body {
            font-family: 'Roboto', sans-serif;
            background: #f5f5f5;
            color: #333;
            line-height: 1.4;
            margin: 0;
            padding: 20px;
            box-sizing: border-box;
        }
        @font-face {
            font-family: 'Roboto';
            src: url('https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=swap');
        }
        .container {
            max-width: 700px;
            margin: auto;
            padding: 10px;
            background: white;
            border-radius: 10px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
            position: relative;
        }
        .header {
            text-align: center;
            color: #2196F3;
            font-size: 3.5em;
            font-weight: bold;
            margin-top: 40px;
            margin-bottom: 10px;
        }
        .back-button {
            position: absolute;
            top: 10px;
            left: 20px;
            cursor: pointer;
            font-size: 3em;
            color: #2196F3;
            text-decoration: none;
        }
        .back-button::before {
            content: '\25C0';
            font-family: 'Roboto';
        }
        .serial-button {
            background: #6699CC;
            border: none;
            color: white;
            padding: 10px 20px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            cursor: pointer;
            border-radius: 5px;
            position: absolute;
            top: 20px;
            right: 20px;
        }
        .devices-container {
            display: grid;
            grid-template-columns: 1fr;
            gap: 15px;
            margin-top: 20px;
        }
        .device {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: #f9f9f9;
            padding: 10px;
            border-radius: 5px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.24);
        }
        .label {
            flex: 1;
            font-size: 1.2em;
        }
        .toggle-button {
            padding: 10px 15px;
            cursor: pointer;
            color: white;
            border: none;
            border-radius: 5px;
            font-size: 16px;
        }
        .toggle-button.on {
            background-color: #4CAF50; /* Green for ON */
        }
        .toggle-button.off {
            background-color: #F44336; /* Red for OFF */
        }
        .toggle-button:not(.on):not(.off) {
            background-color: #ccc; /* Gray for initial state */
        }
        .enable-monitoring-header {
            font-size: 1.5em;
            text-align: center;
            margin-top: 20px;
            margin-bottom: 10px;
        }
        .debug-time {
            text-align: center;
            margin-top: 20px;
        }
        .debug-time-label {
            font-size: 1.2em;
            display: block;
            margin-bottom: 10px;
        }
        .increment-button {
            background: #2196F3;
            border: none;
            color: white;
            padding: 5px 10px;
            margin: 0 5px;
            cursor: pointer;
            border-radius: 5px;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class='container'>
        <a href='/' class='back-button'></a>
        <div class='header'>Settings</div>
        <a href='http://waterfill.local/webserial' style='text-decoration: none;'><button class='serial-button'>Serial</button></a>
        <div class="enable-monitoring-header">Enable Monitoring</div>
        <div class='devices-container'>
    )RAW";

    const char* debugTags[6] = {"TimeDate", "DI", "DO", "AI", "AO", "Control"};
    for (int i = 0; i < 6; i++) {
        bool state = (i == 0) ? enabled.debug.TimeDate : 
                     (i == 1) ? enabled.debug.DI : 
                     (i == 2) ? enabled.debug.DO : 
                     (i == 3) ? enabled.debug.AI : 
                     (i == 4) ? enabled.debug.AO : 
                                enabled.debug.Control;
        html += String("<div class='device'>") +
                String("<div class='label'>") + String(debugTags[i]) + "</div>" +
                String("<button class='toggle-button ") + String(state ? "on" : "off") + "' id='toggle" + String(debugTags[i]) + "' onclick='toggleDebug(\"" + String(debugTags[i]) + "_Debug\")'>" + String(state ? "ON" : "OFF") + "</button>" +
                String("</div>");
    }

    html += R"RAW(
        </div>
        <div class="debug-time">
            <div class="debug-time-label">Serial Print Time Period</div>
            <div>
                <button class='increment-button' onclick='adjustDebugTime(-1)'>-</button>
                <span id='debugTime'>)RAW" + String(timeData.setpoint.Debug) + R"RAW(s</span>
                <button class='increment-button' onclick='adjustDebugTime(1)'>+</button>
            </div>
        </div>
    </div>
</body>

<script>
    function initButtons() {
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (this.readyState == 4 && this.status == 200) {
                var states = JSON.parse(this.responseText);
                for (var tag in states) {
                    var button = document.getElementById('toggle' + tag.split('_')[0]);
                    if (button) {
                        button.textContent = states[tag] ? 'ON' : 'OFF';
                        button.className = 'toggle-button ' + (states[tag] ? 'on' : 'off');
                    }
                }
                // Update debug time on page load
                xhr = new XMLHttpRequest();
                xhr.onreadystatechange = function() {
                    if (this.readyState == 4 && this.status == 200) {
                        document.getElementById('debugTime').textContent = this.responseText;
                    }
                };
                xhr.open('GET', '/getDebugTime', true);
                xhr.send();
            }
        };
        xhr.open('GET', '/getDebugStates', true);
        xhr.send();
    }

    function toggleDebug(tag) {
        var button = document.getElementById('toggle' + tag.split('_')[0]);
        var isOn = button.textContent === 'ON';
        button.textContent = isOn ? 'OFF' : 'ON';
        button.className = 'toggle-button ' + (isOn ? 'off' : 'on');
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/toggleDebug', true);
        xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
        xhr.send('tag=' + tag + '&state=' + !isOn);
    }

    function adjustDebugTime(change) {
        var currentValue = parseInt(document.getElementById('debugTime').textContent);
        var newValue = Math.max(1, Math.min(60, currentValue + change)); // Assuming 1 to 60 seconds
        document.getElementById('debugTime').textContent = newValue;
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/updateDebugTime', true);
        xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
        xhr.send('value=' + newValue);
    }

    window.onload = initButtons;
</script>

</html>
    )RAW";
    return html;
}

