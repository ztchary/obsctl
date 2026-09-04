import websocket

ws = websocket.create_connection("ws://localhost:4455")
print(ws.recv())

