# Play America 250
# How to Start the Game?
Unreal game launch: 
```
arch -arm64 "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/Users/leone/PycharmProjects/Samson/unreal/PlayAmerica250/PlayAmerica250.uproject"
```
Launching the Python:
```
cd ~/PycharmProjects/Samson/services/negotiation\nPA250_LLM=mock PA250_TTS=say PA250_DID=0 MUSETALK_DISABLE=1 \\n  .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port 8000
```