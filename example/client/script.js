let eventSource = null;
let serverAddress = '';
let username = '';

const dialog = document.getElementById('connect-dialog');
const messagesDiv = document.getElementById('messages');
const messageInput = document.getElementById('message-input');
const sendBtn = document.getElementById('send-btn');
const form = document.getElementById('input-form');

document.getElementById('connect-btn').addEventListener('click', connect);
document.getElementById('ip-input').addEventListener('input', () => {
    setFieldError('ip-input', false);
});
document.getElementById('ip-input').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        document.getElementById('port-input').focus();
    }
});
document.getElementById('port-input').addEventListener('input', () => {
    setFieldError('port-input', false);
});
document.getElementById('port-input').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        document.getElementById('username-input').focus();
    }
});
document.getElementById('username-input').addEventListener('input', () => {
    setFieldError('username-input', false);
});
document.getElementById('username-input').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        connect();
    }
});

function setFieldError(fieldId, show) {
    const field = document.getElementById(fieldId);
    field.classList.toggle('input-error', show);
}

function validateIP(ip) {
    if (!ip) {
        return false;
    }
    if (ip === 'localhost') {
        return true;
    }
    const parts = ip.split('.');
    if (parts.length !== 4) {
        return false;
    }
    return parts.every(part => {
        const num = parseInt(part, 10);
        return !isNaN(num) && num >= 0 && num <= 255 && part === String(num);
    });
}

function validatePort(port) {
    if (!port) {
        return false;
    }
    const num = parseInt(port, 10);
    return !isNaN(num) && num > 0 && num <= 65535 && port === String(num);
}

function connect() {
    const ipInput = document.getElementById('ip-input');
    const portInput = document.getElementById('port-input');
    const usernameInput = document.getElementById('username-input');
    
    const ip = ipInput.value.trim();
    const port = portInput.value.trim();
    username = usernameInput.value.trim();

    let valid = true;

    if (!validateIP(ip)) {
        setFieldError('ip-input', true);
        valid = false;
    } else {
        setFieldError('ip-input', false);
    }

    if (!validatePort(port)) {
        setFieldError('port-input', true);
        valid = false;
    } else {
        setFieldError('port-input', false);
    }

    if (!username) {
        setFieldError('username-input', true);
        valid = false;
    } else {
        setFieldError('username-input', false);
    }

    if (!valid) {
        return;
    }

    serverAddress = ip + ':' + port;

    subscribe();
}

function subscribe() {
    if (eventSource) {
        eventSource.close();
    }

    eventSource = new EventSource('http://' + serverAddress + '/subscribe');

    let connected = false;

    function setConnected() {
        if (!connected) {
            connected = true;
            dialog.classList.add('hidden');
            messageInput.disabled = false;
            sendBtn.disabled = false;
            messageInput.focus();
        }
    }

    function showConnectionError() {
        alert('Couldn\'t connect to server');
        dialog.classList.remove('hidden');
    }

    eventSource.onopen = () => {
        setConnected();
    };

    eventSource.onmessage = (e) => {
        setConnected();
        try {
            const msg = JSON.parse(e.data);
            if (msg.type !== 'connected') {
                addMessage(msg);
            }
        } catch (err) {
            console.error('Failed to parse message:', err);
        }
    };

    eventSource.onerror = () => {
        if (!connected) {
            eventSource.close();
            showConnectionError();
        } else {
            messageInput.disabled = true;
            sendBtn.disabled = true;
            dialog.classList.remove('hidden');
        }
    };
}

function addMessage(msg) {
    const time = msg.timestamp ? msg.timestamp.split('T')[1].slice(0, 8) : '';
    const div = document.createElement('div');
    div.className = 'message';
    div.innerHTML = `<span class="message-content"><span class="message-time">[${time}]</span> <span class="message-username">${escapeHtml(msg.username)}:</span> ${escapeHtml(msg.text)}</span>`;
    messagesDiv.appendChild(div);
    messagesDiv.scrollTop = messagesDiv.scrollHeight;
}

function escapeHtml(s) {
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
}

form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const text = messageInput.value.trim();
    if (!text) {
        return;
    }

    messageInput.value = '';

    try {
        const response = await fetch('http://' + serverAddress + '/message', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, text })
        });

        if (!response.ok) {
            console.error('Failed to send message');
        }
    } catch (err) {
        console.error('Error sending message:', err);
    }
});
