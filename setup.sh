#/bin/bash
WEBSOCKET_PID=`cat /tmp/websocket.pid`
kill -9 $WEBSOCKET_PID
serverpath=`pwd`
cmd=$serverpath/"ws_server >> /tmp/websocket.log&"
nohup $cmd > pso.file 2>&1 &
