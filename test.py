import asyncio
from websockets.server import serve

send='{"d":{"authentication":{"challenge":"LYEmI12CbMsZ4EsBceuCZV6AbUp+g6ixFB4DVjtuUTg=","salt":"9LMUtDqzVxE3e8sivy6tvcUUa0ggcPQRUw4VuWzDwrc="},"obsStudioVersion":"32.2.2","obsWebSocketVersion":"5.7.4","rpcVersion":1},"op":0}'

async def echo(websocket):
    await websocket.send(send)

    async for message in websocket:
        print(f"Received: {message}")
        await websocket.send(f"Echo: {message}")

async def main():
    async with serve(echo, "localhost", 8765):
        print("Server started on ws://localhost:8765")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())

