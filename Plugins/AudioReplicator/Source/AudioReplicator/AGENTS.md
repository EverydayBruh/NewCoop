# AGENTS.md

## Purpose

This file is a **specialized guide for AI coding agents** (e.g., OpenAI Codex, Code Interpreter–style tools) to understand and work within the **Audio Replicator** Unreal Engine plugin codebase safely and effectively.

**Important:**
Agents **must not** attempt to launch the Unreal Editor, run Play-In-Editor (PIE), execute builds, install toolchains, or otherwise “test” the environment. Focus on **static code changes**, **reasoning**, and **patch preparation** only.

---

## Project Overview

**Audio Replicator** is a UE plugin that performs:

1. **Local audio transforms**: WAV (PCM16) ⇄ PCM16 ⇄ Opus (framed packets).
2. **Network replication**: Client encodes Opus frames → sends to Server via RPC → Server multicasts to all clients.

Use cases:

* Validating local audio transcode (WAV → Opus packets → WAV).
* Broadcasting short voice clips between clients.
* (Future) Real-time streaming via `USoundWaveProcedural`.

---

## Do/Don’t for Agents

1. **DO**: read/modify C++ and headers; add BP-exposed functions; refactor modules; add guards and docs.
2. **DO**: prepare diffs/patches; suggest changes to `*.Build.cs`, USTRUCT/UCLASS layouts, RPC signatures, and error handling.
3. **DON’T**: build, run, open UE Editor, or execute any system commands.
4. **DON’T**: fetch or install third-party binaries. Assume Opus static lib and headers already exist as documented.

---

## Code Structure (key paths)

```
Plugins/AudioReplicator/
├─ AudioReplicator.uplugin
└─ Source/
   ├─ AudioReplicator/
   │  ├─ AudioReplicator.Build.cs
   │  ├─ Public/
   │  │  ├─ AudioReplicatorBPLibrary.h        // BP nodes: local encode/decode + utilities
   │  │  ├─ AudioReplicatorComponent.h        // Network RPC component (Server/Multicast)
   │  │  ├─ OpusCodec.h                       // Thin C++ wrapper around libopus
   │  │  ├─ PcmWavUtils.h                     // Minimal WAV RIFF (PCM16) read/write
   │  │  ├─ Chunking.h                        // Packet buffer pack/unpack helpers
   │  │  └─ OpusTypes.h                       // USTRUCTs for BP-safe packet types
   │  └─ Private/
   │     ├─ AudioReplicatorModule.cpp
   │     ├─ AudioReplicatorBPLibrary.cpp
   │     ├─ AudioReplicatorComponent.cpp
   │     ├─ OpusCodec.cpp
   │     ├─ PcmWavUtils.cpp
   │     └─ Chunking.cpp
   └─ ThirdParty/
      └─ Opus/
         ├─ Opus.Build.cs
         ├─ Include/          // opus.h, etc.
         └─ Lib/Win64/Release/opus.lib
```

---

## Third-Party Dependency (Opus)

* **Static linking** with libopus. Build rules in `ThirdParty/Opus/Opus.Build.cs`.
* Headers in `ThirdParty/Opus/Include/`; library in `ThirdParty/Opus/Lib/.../opus.lib`.
* Default params: **48 kHz**, **20 ms frames (960 samples/channel)**, mono/stereo supported.

---

## Public API Surface (BP-facing)

### Data Types (`OpusTypes.h`)

1. `USTRUCT FOpusPacket`

   * `TArray<uint8> Data` — a single Opus frame payload.

2. `USTRUCT FOpusStreamHeader`

   * `int32 SampleRate` (default 48000)
   * `int32 Channels` (1 or 2)
   * `int32 Bitrate` (e.g., 32000 for mono)
   * `int32 FrameMs` (e.g., 20)
   * `int32 NumPackets` (optional metadata for pre-sizing)

3. `USTRUCT FOpusChunk`

   * `int32 Index` — sequential frame index starting at 0.
   * `FOpusPacket Packet` — one encoded frame.

> **Design note:** UE’s UHT disallows nested arrays like `TArray<TArray<uint8>>` in UFUNCTION/UPROPERTY. We therefore **wrap per-frame data in `FOpusPacket`** to expose `TArray<FOpusPacket>` to Blueprints.

### Local Transform Nodes (`AudioReplicatorBPLibrary`)

* `LoadWavToPcm16(WavPath) -> Pcm16:int32[], SampleRate:int32, Channels:int32, ok`
* `EncodePcm16ToOpusPackets(Pcm16[], SR, Ch, Bitrate, FrameMs) -> Packets:FOpusPacket[], ok`
* `DecodeOpusPacketsToPcm16(Packets[], SR, Ch) -> Pcm16:int32[], ok`
* `SavePcm16ToWav(OutPath, Pcm16[], SR, Ch) -> ok`
* `PackOpusPackets(Packets[]) -> FlatBuffer:uint8[]` (for file/buffer persistence)
* `UnpackOpusPackets(FlatBuffer[]) -> Packets[]`
* `TranscodeWavToOpusAndBack(InWavPath, OutWavPath, Bitrate=32000, FrameMs=20) -> ok` (smoke test)

### Networking Component (`AudioReplicatorComponent`)

Attach to a **replicated actor** (prefer `PlayerController`).

**Blueprint Events**

* `OnTransferStarted(SessionId:Guid, Header:FOpusStreamHeader)`
* `OnChunkReceived(SessionId:Guid, Chunk:FOpusChunk)`
* `OnTransferEnded(Source:AudioReplicatorComponent, SessionId:Guid)`

**Blueprint Calls (Owning Client)**

* `StartBroadcastFromWav(WavPath, Bitrate, FrameMs) -> SessionId:Guid, ok`
* `StartBroadcastOpus(Packets[], Header) -> SessionId:Guid, ok`
* `CancelBroadcast(SessionId)`
* `GetReceivedPackets(SessionId) -> Packets[], Header, ok`

**RPCs (internal)**

* Server: `Server_StartTransfer(SessionId, Header)` **Reliable**
* Server: `Server_SendChunk(SessionId, Chunk)` **Unreliable** 
* Server: `Server_EndTransfer(SessionId)` **Reliable**
* Multicast: `Multicast_StartTransfer(SessionId, Header)` **Reliable**
* Multicast: `Multicast_SendChunk(SessionId, Chunk)` **Unreliable**
* Multicast: `Multicast_EndTransfer(SessionId)` **Reliable**

---

## Networking Semantics & Rationale



---

## Known Constraints & Assumptions

1. WAV I/O supports **PCM16 LE** only. Non-PCM16 (e.g., float/24-bit) should be converted upstream (future: add resampling/format conversion).
2. Default sample rate **48 kHz**; frame size = `SR/1000 * FrameMs` per channel (e.g., 48000/1000\*20 = 960).
3. Stereo is supported, but bitrates should be scaled up accordingly (e.g., 48–64 kbps).
4. `Chunking` pack/unpack uses 16-bit length prefixes (per packet limit 65535 bytes; Opus frames are far smaller).
5. No property replication is used; transport is entirely via RPC calls (Server/Multicast).

---

## Common Pitfalls (and how the code avoids them)

1. **UHT nested TArray**: `TArray<TArray<uint8>>` is **invalid** in UFUNCTION/UPROPERTY. Use `FOpusPacket`.
2. **PLC length bloat**: Do **not** decode empty packets. `GetReceivedPackets` filters to **non-empty** frames only.
3. **Duplicates**: Receiver should not increment counters for duplicate indices. Code guards are present.
4. **Actor replication**: The component must live on a **replicated actor**; best attached to `PlayerController`.
5. **Owning client**: Start RPC flow **from the owner** (client) so `Server_*` executes.

---


## Coding Conventions

* Follow Unreal Engine coding style (PascalCase for types, UpperCamel UCLASS/USTRUCT names, macros above declarations).
* Keep Blueprint-callable functions **minimal and deterministic**.
* Keep `Public/` headers light; prefer `Private/` for implementation details.
* Avoid nested containers in UFUNCTION/UPROPERTY; wrap in `USTRUCT`.
* Guard all external input (indices, sizes, paths).

---

## Build & Modules (static guidance only)

* `AudioReplicator.Build.cs` depends on: `"Core", "CoreUObject", "Engine", "NetCore"`.
* `ThirdParty/Opus/Opus.Build.cs` adds include path and `opus.lib` for Win64; define `OPUS_STATIC=1`.
* **Agents must not build**. Provide changes in code only.

---

## Blueprint Usage Pattern (reference)

**Sender (owning client):**

1. `StartBroadcastFromWav(WavPath, Bitrate=32000, FrameMs=20)`
   → Component sends `Header` (Reliable), then frames (`Server_SendChunk`), then `End` (Reliable).

**Receivers (all clients):**

1. On `OnTransferStarted`: allocate UI progress; cache `Header`.
2. On `OnChunkReceived`: update progress by `Received / Header.NumPackets`.
3. On `OnTransferEnded`:

   * `GetReceivedPackets` → `DecodeOpusPacketsToPcm16` → `SavePcm16ToWav` (or enqueue to procedural wave for playback).

---

## Security & Robustness Notes

* Validate sizes before allocation; reject packets > 65535 bytes.
* Treat `NumPackets` as a hint; still harden against malformed indices.
* Avoid writing to arbitrary file paths from untrusted inputs.
* Never decode empty packets (prevents PLC inflating content).


---

## Contribution Guidance (for agents)

* Prepare **minimal diffs** touching only necessary files.
* Respect module boundaries (`Public/` vs `Private/`).
* For new BP nodes, provide: short doc comment, input validation, failure logs.
* Document any network change (reliability/ACKs) in this file’s **Networking Semantics** section.
* Do not introduce runtime “tests”; instead, add comments and example BP wiring snippets where helpful.

---

## Non-Goals

* Automated environment setup, building, or executing the project.
* Editor tooling, UI/UX beyond simple node additions and events.
* Large dependency changes or platform expansion without explicit instruction.

---

