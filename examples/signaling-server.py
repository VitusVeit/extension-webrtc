import sys
import ssl
import json
import random
import string
import asyncio
import logging
import websockets


logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))

peers = {}


class Peer:
    def __init__(self, websocket, id):
        self.ws = websocket
        self.id = id
    
    async def send(self, message_type, data):
        await self.ws.send(json.dumps({
            'type': message_type,
            'data': data,
        }))


async def handle_websocket(websocket, path):
    client_id = None
    
    try:
        peer_id = random.randint(0, 99)
        print('Peer {} connected'.format(peer_id))

        peers[peer_id] = Peer(websocket, peer_id)
        await peers[peer_id].send("id", peer_id)
        while True:
            dump = await websocket.recv()
            
            message = json.loads(dump)
            data = message['data']

            lst = data.split('@EOS@')
            destination_id = int(lst[0])

            destination_websocket = peers.get(destination_id)
            if destination_websocket:
                data = str(peer_id) + data[data.find('@EOS@'):]
                print('Client {} >> {}'.format(destination_id, data))
                await destination_websocket.send("connection", data)
            else:
                print('Client {} not found'.format(destination_id))

    except Exception as e:
        print(e)

    finally:
        if peer_id:
            del peers[peer_id]
            print('Client {} disconnected'.format(peer_id))


async def main():
    # Usage: python3 server.py [[host:]port] [ssl_certificate] [private_key]

    endpoint = sys.argv[1] if len(sys.argv) > 1 else None
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None
    prv_key = sys.argv[3] if len(sys.argv) > 3 else None

    if not endpoint:
        print('Endpoint not given, closing server.')
        return

    if ssl_cert and prv_key:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert, prv_key)
        print('SSL context created, starting secure connection.')
    else:
        ssl_context = None
        print('SSL context not given, starting unsecure connection.')

    print('Listening on {}'.format(endpoint))
    host, port = endpoint.rsplit(':', 1)

    server = await websockets.serve(handle_websocket, host, int(port), ssl = ssl_context)
    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())