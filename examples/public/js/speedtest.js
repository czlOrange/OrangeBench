let ws = null;
let isTesting = false;
let totalBytes = 0;
let startTime = 0;
let packetSeq = 0;
let receivedSeq = 0;
let lostCount = 0;
let pingStart = 0;
let chart;
let speedData = [];

window.onload = () => {
    const ctx = document.getElementById('speedChart').getContext('2d');
    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: '速率 (Mbps)',
                data: [],
                borderColor: '#2ecc71',
                fill: false
            }]
        },
        options: {
            responsive: true,
            scales: { y: { beginAtZero: true } }
        }
    });
    document.getElementById('startBtn').onclick = startTest;
    document.getElementById('stopBtn').onclick = stopTest;
};

function startTest() {
    if (ws && ws.readyState === WebSocket.OPEN) ws.close();
    ws = new WebSocket('ws://' + location.host + '/speedtest');
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => {
        document.getElementById('statusText').innerHTML = '已连接，测速中...';
        document.getElementById('startBtn').disabled = true;
        document.getElementById('stopBtn').disabled = false;
        isTesting = true;
        totalBytes = 0;
        startTime = Date.now();
        packetSeq = 0;
        receivedSeq = 0;
        lostCount = 0;
        speedData = [];
        chart.data.labels = [];
        chart.data.datasets[0].data = [];
        ws.send('start');
        // 每 1 秒发送一次 Ping 测延迟
        setInterval(() => {
            if (isTesting && ws.readyState === WebSocket.OPEN) {
                pingStart = Date.now();
                ws.send('ping');
            }
        }, 1000);
    };
    ws.onmessage = (event) => {
        if (event.data === 'pong') {
            let latency = Date.now() - pingStart;
            document.getElementById('latency').innerText = latency;
            return;
        }
        if (typeof event.data === 'string') {
            // 可能是丢包报告等
            console.log(event.data);
            return;
        }
        // 二进制数据
        totalBytes += event.data.byteLength;
        let elapsed = (Date.now() - startTime) / 1000;
        let speedMbps = (totalBytes * 8) / elapsed / 1e6;
        document.getElementById('downloadSpeed').innerHTML = speedMbps.toFixed(2);
        // 更新图表
        speedData.push(speedMbps);
        if (speedData.length > 60) speedData.shift();
        chart.data.labels = speedData.map((_, i) => i);
        chart.data.datasets[0].data = speedData;
        chart.update();
        // 丢包统计（假设服务器发送带序号的数据包）
        // 这里简化，实际需要服务器在二进制数据前加序号
    };
    ws.onclose = () => {
        if (isTesting) stopTest();
    };
    ws.onerror = (e) => {
        console.error(e);
        stopTest();
    };
}

function stopTest() {
    isTesting = false;
    if (ws) ws.close();
    document.getElementById('statusText').innerHTML = '已停止';
    document.getElementById('startBtn').disabled = false;
    document.getElementById('stopBtn').disabled = true;
    // 最终统计
    let elapsed = (Date.now() - startTime) / 1000;
    let avgSpeed = totalBytes * 8 / elapsed / 1e6;
    let lossRate = lostCount / (packetSeq + 1) * 100;
    document.getElementById('log').innerHTML = `测试完成，平均速率 ${avgSpeed.toFixed(2)} Mbps，丢包率 ${lossRate.toFixed(2)}%`;
}