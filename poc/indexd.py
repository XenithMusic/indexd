#!/usr/bin/env python3

import json,asyncio,sqlite3,os

db = sqlite3.connect("index.db")
cur = db.cursor()

def warn(txt,*args,**kwargs):
    print("(warn)",txt,*args,**kwargs)

async def con(reader:asyncio.StreamReader,writer:asyncio.StreamWriter):
    async def win(x):
        x["success"] = True
        writer.write(json.dumps(x).encode())
        writer.close()
        await writer.wait_closed()
        asyncio.current_task().cancel("Success")
    async def err(x:dict):
        x["success"] = False
        writer.write(json.dumps(x).encode())
        writer.close()
        await writer.wait_closed()
        asyncio.current_task().cancel("Failure")
    data = json.loads((await reader.read(1024)).decode())
    if not "type" in data:
        await err({"why":"Malformed","how":"missing","which":"type"})
    match data["type"]:
        case "query":
            await win({"yay":"wahoo"})
        case _:
            await err({"why":"Malformed","how":"incorrect","which":"type"})
        

async def main():
    if os.path.exists("/run/indexd.sock"):
        os.remove("/run/indexd.sock")
        print("removing stale socket")
    server = await asyncio.start_unix_server(con,path="/run/indexd.sock")
    async with server:
        await server.serve_forever()

try:
    asyncio.run(main())
except KeyboardInterrupt:
    pass
print("Dying peacefully...")
if os.path.exists("/run/indexd.sock"):
    os.remove("/run/indexd.sock")
    print("removing stale socket")
cur.close()
db.close()