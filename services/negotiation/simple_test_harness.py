# Ultra-simple test harness for negotiation service
from __future__ import annotations
import asyncio, os, uuid
from typing import Dict, Any
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import HTMLResponse, PlainTextResponse
from starlette.websockets import WebSocketState
from ruamel.yaml import YAML
from pydantic import BaseModel

yaml = YAML()

app = FastAPI(title="Simple Negotiation Test Harness")

SESSIONS: Dict[str, Dict[str, Any]] = {}

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def _dump_yaml(obj: Any) -> str:
    from io import StringIO
    buf = StringIO()
    yaml.dump(obj, buf)
    return buf.getvalue()

# Simple mock provider for testing
class SimpleMockProvider:
    def __init__(self):
        self.counter = 0
    
    async def stream_dialogue(self, turns, world_context, system_guidelines):
        """Generate simple mock responses."""
        await asyncio.sleep(0.5)  # Simulate processing
        
        # Extract last player message
        player_text = ""
        for turn in reversed(turns):
            if turn.get("speaker") == "PLAYER":
                player_text = turn.get("text", "")
                break
        
        # Generate subtitle
        yield {
            "type": "subtitle",
            "payload": {"text": f"Processing: {player_text[:50]}..."},
            "is_final": False
        }
        
        await asyncio.sleep(1.0)
        
        yield {
            "type": "subtitle", 
            "payload": {"text": f"Understood: {player_text}"},
            "is_final": True
        }
        
        await asyncio.sleep(0.5)
        
        # Generate intent based on keywords
        if "trade" in player_text.lower() and "withdraw" in player_text.lower():
            intent = {
                "kind": "COUNTER_OFFER",
                "confidence": 0.85,
                "summary": "Trade access for troop withdrawal",
                "details": {
                    "offer": "Trade access to colonial ports",
                    "demand": "Withdrawal of troops from Ohio Country"
                }
            }
        elif "ceasefire" in player_text.lower() and "or else" in player_text.lower():
            intent = {
                "kind": "ULTIMATUM", 
                "confidence": 0.92,
                "summary": "Ceasefire demand with war threat",
                "details": {
                    "demand": "Immediate ceasefire",
                    "consequence": "Declaration of war"
                }
            }
        else:
            intent = {
                "kind": "PROPOSAL",
                "confidence": 0.75,
                "summary": "General diplomatic proposal",
                "details": {
                    "topic": "Diplomatic relations",
                    "stance": "Cooperative"
                }
            }
        
        yield {
            "type": "intent",
            "payload": intent
        }
        
        # Safety check
        yield {
            "type": "safety",
            "payload": {
                "is_safe": True,
                "reason": "Content passed all safety checks",
                "flags": []
            }
        }

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve a simplified test page without WebRTC video
    html_content = """
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Simple Negotiation Test Client</title>
  <style>
    body { font-family: system-ui, Arial; margin: 20px; }
    #subs { border: 1px solid #ccc; padding: 8px; width: 480px; height: 120px; overflow: auto; white-space: pre-wrap; }
    .row { display:flex; gap:20px; margin-top:10px;}
    button { padding: 8px 16px; margin: 5px; }
    textarea { width: 400px; height: 100px; }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/js-yaml@4.1.0/dist/js-yaml.min.js"></script>
</head>
<body>
  <h1>Simple Negotiation Test (WebSocket + YAML)</h1>

  <div class="row">
    <div>
      <label>Model:
        <select id="model">
          <option value="mock_local">Mock Local</option>
          <option value="veo3">Veo3 (stub/fallback)</option>
        </select>
      </label>
      <button id="create">Create Session</button>
      <p>Session: <code id="sid">—</code></p>
      <div id="subs"></div>
    </div>
    <div>
      <textarea id="utter" rows="6" cols="40" placeholder="Say something to your envoy…"></textarea><br/>
      <button id="send">Send Utterance</button>
      <p>Status: <span id="status">idle</span></p>
    </div>
  </div>

<script>
(async function() {
  const ydump = obj => jsyaml.dump(obj);
  const yload = str => jsyaml.load(str);
  const $ = id => document.getElementById(id);

  let sid = null;
  let ws = null;

  async function createSession() {
    const model = $("model").value;
    const res = await fetch("/v1/session", {
      method: "POST",
      headers: {"Content-Type":"application/x-yaml"},
      body: ydump({ model })
    });
    const text = await res.text();
    const data = yload(text);
    sid = data.session_id;
    $("sid").textContent = sid;
    $("status").textContent = "session created";
    await openWS();
  }

  async function openWS() {
    ws = new WebSocket(`ws://${location.host}/v1/session/${sid}/control`);
    ws.onopen = () => $("status").textContent = "ws open";
    ws.onmessage = (ev) => {
      const obj = yload(ev.data);
      if (obj.type === "subtitle") {
        $("subs").textContent += (obj.final ? "🟢 " : "… ") + obj.text + "\\n";
        $("subs").scrollTop = $("subs").scrollHeight;
      } else if (obj.type === "intent") {
        $("subs").textContent += "📜 INTENT:\\n" + ydump(obj.payload) + "\\n";
        $("subs").scrollTop = $("subs").scrollHeight;
      } else if (obj.type === "safety") {
        $("subs").textContent += "⚠️ SAFETY: " + JSON.stringify(obj.payload) + "\\n";
      }
    };
    ws.onclose = () => $("status").textContent = "ws closed";
  }

  $("create").onclick = createSession;
  $("send").onclick = () => {
    if (ws && ws.readyState === WebSocket.OPEN) {
      const text = $("utter").value.trim();
      if (text) ws.send(ydump({ type: "player_utterance", text }));
    }
  };
})();
</script>
</body>
</html>
    """
    return HTMLResponse(html_content)

@app.post("/v1/session", response_class=PlainTextResponse)
async def create_session(request: Request):
    body = yaml.load(await request.body() or b"") or {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "mock_local")
    
    SESSIONS[session_id] = {
        "model": model,
        "ws_clients": set(),
        "turns": [],
        "world_context": body.get("world_context", {}),
        "provider_task": None,
    }
    return _dump_yaml({"session_id": session_id})

@app.post("/v1/session/{sid}/webrtc/offer", response_class=PlainTextResponse)
async def sdp_offer(sid: str, sdp_in: SDPIn):
    # For this simple version, just return a dummy response
    return _dump_yaml({"type": "answer", "sdp": "v=0\\r\\no=- 0 0 IN IP4 127.0.0.1\\r\\ns=-\\r\\nt=0 0\\r\\n"})

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)

    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            await ws.send_text(_dump_yaml(ev))

    # Start provider loop
    async def provider_loop():
        provider = SimpleMockProvider()
        turns = sess["turns"] or [{"speaker":"PLAYER","text":"We'll grant trade access if you withdraw troops from Ohio Country."}]
        
        async for ev in provider.stream_dialogue(
            turns=turns,
            world_context=sess["world_context"],
            system_guidelines="Test harness mode"
        ):
            if ev["type"] == "subtitle":
                await send_yaml({"type":"subtitle","text": ev["payload"].get("text",""), "final": ev.get("is_final", False)})
            elif ev["type"] == "intent":
                await send_yaml({"type":"intent","payload": ev["payload"]})
            elif ev["type"] == "safety":
                await send_yaml({"type":"safety","payload": ev["payload"]})

    task = asyncio.create_task(provider_loop())
    sess["provider_task"] = task

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                sess["turns"].append({"speaker":"PLAYER","text":text})
                # Restart provider with new turn
                if not task.done():
                    task.cancel()
                task = asyncio.create_task(provider_loop())
                sess["provider_task"] = task
            # Echo ack
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not task.done():
            task.cancel()
        sess["ws_clients"].discard(ws)
