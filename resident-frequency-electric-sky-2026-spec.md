# Resident Frequency — Electric Sky 2026
## Working Technical Spec
*Last updated: June 12, 2026*

---

## Project Overview

**Project name:** Resident Frequency  
**Event:** Electric Sky 2026: Super Natural  
**Dates:** July 30 – August 2, 2026  
**Location:** Skykomish Ballpark, Skykomish WA (next to the Skykomish River)  
**Grant:** $425 from Third Place Technologies / Sky Artworks (accepted pending reply)  
**Free tickets:** 3 total — Evan Chakroff, Christopher Saldanha, Tom Henderson  
**Note:** Kevin Landesman is a newer collaborator not yet on the free ticket list — clarify with Shelly

**One-paragraph description (from grant application):**
The project is a 12'x12' popup tent installation equipped with an immersive speaker system, multiple projectors, and a custom signal router running on a Raspberry Pi that pulls data from any source and makes it shareable as generative OSC/MIDI source material. At rest, the space performs an ambient music (modular synth) and video (p5.js) composition built on audio and atmospheric data from environmental sensors in the nearby woods and riverbank. Participants modify the composition in several ways, intentionally or not — through distance and motion sensors, MIDI controllers, microphones, or laptops. Feedback loops emerge as the generative output of one composition becomes seed data for another one, with unpredictable results out of the control of any artist or participant.

---

## People

| Name | Role | Contact |
|---|---|---|
| Adrian MacDonald | Project lead, signal router, ESP32 stations, p5.js | adrmac@gmail.com / adrian@postoccupancy.com |
| Evan Chakroff | Modular synth, Kinect, projectors, 5.1 speakers | echakroff@gmail.com |
| Christopher Saldanha | Audio producer | christophersaldanha@gmail.com |
| Tom Henderson | Generative visuals, shaders | tom@mathpunk.net |
| Kevin Landesman | Theatrical projection, tent layout/rigging | kevinlandesman@gmail.com |
| Shelly Farnham | Festival organizer | shelly@thirdplacetechnologies.com / (206) 226-3586 |
| Jeff (co-organizer) | Festival | nasacar@gmail.com / 206-579-0233 |

**Note on Evan:** Primary collaborator, architect by background. VENT! at Georgetown Steam Plant (June 28) is his separate project; Electric Sky is Adrian's. Evan flying to Portugal shortly after VENT — duration of Electric Sky attendance uncertain.

---

## Festival Logistics

**Schedule:**
- Thu July 30: Arrive after 2pm for early setup. Maker lab installed by end of day.
- Fri July 31: Arrive by 7pm for opening meetup
- Sat Aug 1: Installation complete by 7pm. Group dinner 6pm. Art show 8–11pm.
- Sun Aug 2: Personal camps down by noon. Full project takedown by 7pm.
- Sep 30: Receipt submission deadline for reimbursement

**Outstanding actions with Shelly:**
- [ ] Reply to accept the grant (email from June 2, unanswered)
- [ ] Ask about borrowing a tent (budget includes $200 for tent — may save money)
- [ ] Confirm Kevin Landesman for free ticket or clarify collaborator list
- [ ] Join Discord before July 2 expiry: https://discord.gg/Xx3MSsm3j
- [ ] Provide promotion info: project title, participant names, 2-sentence description, project materials, photo, IG/portfolio

**Installation requirements:**
- Power for laptops, speakers, projectors, interior lights
- WiFi helpful but optional (self-contained network)
- Positioned away from strong outdoor flood lights (projection washout risk)

---

## Hardware / Inventory

### Existing inventory (not grant-funded)
- Sound system: receiver, 4 speakers, 1 subwoofer
- 2–3 basic projectors (Evan has 3, varying brightness)
- 5.1 speaker system with inflatable bean bag chairs (Evan)
- Microcontrollers: Raspberry Pi 3, ESP32, Arduino Uno
- Sensors: Kinect (Evan), low-res temperature, HC-SR04 ultrasonic distance
- Ethernet switch and cables
- WiFi extender
- Microphones, USB audio interface
- Various MIDI controllers
- Extension cords, power strips

### Grant budget items ($425 total)
- 12x12 party tent — $200 (ask Shelly about borrowing first — may save entire line)
- Projection surfaces — $100
- Projector mounting — $150
- Gaffer tape — $20
- Sensor upgrades: high-res temperature, INMP441 wireless audio — $50 *(original application; see revised station BOM below)*
- Solar panel / battery / enclosure for riverbank ESP32 — $50 *(original application; actual cost ~$28/station for power components alone — see revised BOM)*
- Raspberry Pi 4 (wishlist) — $80
- Weatherproof ziplock bags — $20
*(Total requested $670, awarded $425 — prioritize tent, mounting, solar station)*

**Revised budget scenarios (post-application):**
- Borrow tent from Shelly, 1 prototype station, no Pi 4: ~$69 station components — grant covers it
- Buy tent, 1 station, no Pi 4: ~$269 — out of pocket ~$0 (fits in grant if tent ≤$200)
- 3 remote stations (full build, no tent): ~$207 station components — grant covers with room for mounting/surfaces
- 3 stations + tent: ~$407 — very tight; prioritize tent borrow

**Assembly tools (out of pocket, one-time, ~$73):** See prototype BOM section below. Not grant-eligible as pre-festival workshop equipment.

---

## Prototype Build BOM
*Order by June 12–13 for next-week delivery. Minimum viable: one complete sensing station.*

**Prototype MCU:** ESP32-S3-DevKitC-1 (already owned). Firmware ports directly to Freenove ESP32-S3 CAM for final deployment — same S3 chip, same toolchain.

**Assembly approach:** Solder components to perfboard using female pin headers (boards plug in, don't solder directly). Wire connections with 22AWG hookup wire + heat shrink for strain relief. Mounts inside IP65 Adafruit flanged enclosure with clear polycarbonate lid (camera window resolved). This is through-hole work on pre-made breakout boards — not fine SMD soldering. Makerspace session recommended for first build.

### Station components (1 unit ~$100)

| Part | Qty | ~Price | Notes |
|---|---|---|---|
| Freenove ESP32-S3 CAM (16MB Flash) | 1 | $22 | See board selection rationale below |
| BME280 breakout, I2C, 3.3V | 1 | $5 | 4-pack available ~$19 |
| INMP441 MEMS mic breakout | 1 | $6 | I2S; primary ambient audio sensor |
| TP4056 charging module w/ protection | 1 | $1 (5-pack $5) | "With protection" variant required |
| 18650 cells × 2 + 2-slot holder | 1 set | $9 | EVE INR18650/29V flat top, name-brand |
| Waveshare 6V 5W solar panel | 1 | $25 | 6V required; rigid glass for durability and reuse |
| Adafruit flanged enclosure w/ clear lid | 1 | $10 | IP65, PG-7 cable glands built in; clear lid resolves camera window |
| CCTV arm bracket, adjustable angle | 1 | $10 | Pole/stake/wall mount for solar panel; not flat-roof Z-brackets |
| AcuRite solar radiation shield | 1 | $15 | Louvered housing for BME280 external mount |
| ~~Acoustic vent membrane stickers~~ | — | — | Skipped. Small hole drilled in enclosure bottom face (downward-facing = rain-shaded). Adequate for 4-day festival; revisit for permanent deployments. |
| INA219 I2C current/voltage sensor | 1 | $2–4 | Sits on same I2C bus as BME280; measures battery voltage + bidirectional charge/draw current. Charging current = solar irradiance proxy. |
| Cable glands PG-7 extra (pack) | 1 | $6 | Additional glands for sensor and power cables |

### Assembly tools (~$73, one-time)

| Part | Qty | ~Price | Notes |
|---|---|---|---|
| Soldering iron — Pinecil USB-C | 1 | $28 | USB-C powered, good temp control |
| Solder, 60/40 rosin core, .031" | 1 | $8 | Thin solder easier for small boards |
| Perfboard, 5-pack | 1 | $6 | |
| Heat shrink assortment | 1 | $8 | |
| 22AWG hookup wire assortment | 1 | $10 | |
| Female pin headers, 40-pin strips | 1 pack | $5 | Socket boards rather than soldering direct |
| Flush cutters | 1 | $8 | |

**Total to order: ~$142.** Station components grant-eligible; tools are out of pocket.

**Not needed yet:** GL.iNet router (use home WiFi for bench testing), cable glands, additional station components.

**Parallel software task:** OSC/UDP firmware for ESP32 can be built now using prototype DevKit — no new hardware required. ESP32 sends OSC packets directly to Pi; no MQTT broker needed for musical data.

---

## Sensing Stations

### Station locations (3–5 total)
- `river` — riverbank, solar-powered, remote
- `forest` — trees near camp, solar-powered, remote
- `elevated` — higher elevation, solar-powered, remote (optional)
- `tent` — interior, wired, continuous streams

### Preferred deployment board: Freenove ESP32-S3 CAM (16MB Flash)
Replaces original ESP32-CAM (AI Thinker) and supersedes earlier XIAO ESP32S3 Sense recommendation.

**Why Freenove over original ESP32-CAM:** Original ESP32-CAM uses older ESP32 (LX6 core, 520KB SRAM, 4MB Flash). Freenove uses ESP32-S3 (LX7 dual-core, 8MB PSRAM, 16MB Flash) — same chip as the prototype DevKit. No firmware rewrite required. Original CAM had pin starvation and strapping pin issues with I2S + I2C; S3 resolves this.

**Why Freenove over XIAO ESP32S3 Sense:** Once INMP441 sensors were ordered, the Sense's built-in PDM mic became redundant — it's the main thing you pay a premium for. Freenove at $22 (Amazon Prime) vs. Sense at $26 (Seeed international shipping, slower delivery). Freenove has better tutorial coverage and more community examples for OV2640 camera firmware — meaningful for first-time implementation. Slightly larger form factor is manageable in the Adafruit enclosure. Both use OV2640 camera and the same S3 chip.

**Why not XIAO ESP32-C3, C6, or other XIAO variants:** Evaluated full Seeed XIAO lineup. All WiFi-capable boards except the S3 Sense lack camera interface. C6 noted for future weather-only nodes (WiFi 6, better power management). nRF52840 Sense noted for future wearable/indoor sensing (BLE, IMU, mic — no WiFi).

**Why not HiLetgo ESP32-CAM 2-pack ($9.25/unit):** Original ESP32 chip (not S3), 520KB SRAM, requires physical hardware intervention to flash firmware (GPIO 0 boot mode), all pin starvation issues apply. False economy.

**Future board options noted:**
- XIAO ESP32-C6: WiFi 6, better power management, no camera — good for weather-only nodes
- XIAO nRF52840 Sense: BLE, IMU, mic — no WiFi, future wearable/indoor sensing work

### Hardware per remote station
- Freenove ESP32-S3 CAM 16MB (MCU + OV2640 camera)
- INMP441 digital MEMS microphone (I2S, external, primary audio)
- BME280 (temp, humidity, barometric pressure) — mounted outside enclosure in radiation shield
- Waveshare 6V 5W solar panel — rigid glass, aluminum frame, screw mounting
- 2× 18650 cells in parallel (name-brand) + 2-slot holder
- TP4056 charging module with protection circuit
- Adafruit flanged weatherproof enclosure (IP65, PG-7 cable glands built in)
- Adjustable tilt bracket (CCTV-style) for post/stake/tree mounting
- Solar radiation shield (louvered housing) for BME280 external mount
- Acoustic vent membrane sticker over INMP441 mic port hole in enclosure
- Cable glands (PG-7) for solar, sensor, and any additional cable entries

### Power budget (remote stations)
- **Continuous operation (no deep sleep):** ~200–300mA without camera, ~300–400mA with camera active
- 6V 5W panel at 50% real-world efficiency: ~400mA — covers continuous draw during daylight
- 2× 18650 at 5700mAh: ~14–28 hours overnight buffer at continuous draw (adequate for 4-day festival)
- Battery charges during the day; net positive in reasonable sun conditions
- Camera duty-cycled (snapshot every 30s) to save 80–120mA — camera not needed continuously for musical data
- Continuous raw audio streaming explicitly ruled out (power draw + no added value over FFT)
- 5V nominal panels rejected for insufficient TP4056 headroom

### Audio and sensor architecture (resolved)
Raw audio streaming is ruled out — outdoor stations do FFT on-device. The river's spectral character shapes the composition; river sound is not directly reproduced. Continuous raw audio remains available for the wired tent station only.

**Transport decision (resolved):** OSC/UDP directly from ESP32 to Pi for all musical data. No MQTT broker. UDP is fire-and-forget with no TCP overhead — same protocol OSCtopus already speaks. Dropped packets at musical rates are imperceptible. HTTP→Supabase retained for logging (temp/humidity/pressure every 30s).

### Power management approach
- **Continuous operation, no deep sleep** during festival hours
- WiFi connection maintained; OSC/UDP packets sent continuously
- Camera snapshots every 30s (duty-cycled to save 80–120mA)
- Reduced WiFi TX power at low battery
- TP4056 protection circuit handles under-voltage cutoff

### Sensor specs
| Sensor | Model | Data | Rate | Protocol |
|---|---|---|---|---|
| Audio FFT | INMP441 MEMS | Frequency band summary | 120BPM (500ms) or faster | OSC/UDP |
| Pressure | BME280 | Barometric pressure | 10–20Hz (musically useful — responds to wind, crowd) | OSC/UDP |
| Temp/humidity | BME280 | Temperature, humidity | 1Hz (physically slow sensors; useful for slow drift) | OSC/UDP |
| Camera | OV2640 | 640×480 JPEG snapshot | Every 30s | HTTP |
| Power telemetry | INA219 | Battery voltage, charge/draw current | 1–10Hz via OSC/UDP | OSC/UDP |
| Solar irradiance (derived) | INA219 | Charging current as solar proxy | 1–10Hz via OSC/UDP | OSC/UDP |
| Logging | BME280 + INA219 | Temp, humidity, pressure, battery state | Every 30s | HTTP→Supabase |
| Human presence (tent) | OV2640 | Motion/blob detection | Continuous (wired) | OSC/UDP |
| Human presence (tent) | HC-SR04 | Distance, available as MIDI | On-demand | OSC/UDP |

**IAQ sensor** (indoor air quality): desirable but no model decided.  
**LiDAR:** Explicitly excluded from the observatory tent. Reserved for Evan's separate VENT! monks piece.

---

## Networking

### Communication protocol: WiFi
*Decided in favor of WiFi over LoRa, cellular, or Bluetooth mesh.*

Rationale: Skykomish venue is a campground in a ~100m radius — manageable WiFi range with a dedicated AP. LoRa was evaluated and rejected: it requires a separate LoRa gateway translating to OSC/JSON for the router, adding hardware complexity. Cellular (HTTP→Supabase, already working on bench) was rejected for latency reasons. WiFi keeps stations on the same direct signal chain.

- Central access point in tent handles all station connections
- GL.iNet GL-MT300N-V2 travel router (~$35) with high-gain antenna as dedicated AP
- Pi ethernet switch as backbone for wired tent devices
- **Open:** GL.iNet router vs. Pi's existing `ap-mode`/`wifi-mode` scripts

### Transport: OSC/UDP (musical data) + HTTP (logging)

**Musical data — OSC/UDP directly to Pi:**
ESP32 stations send OSC packets to OSCtopus on port 5005. No MQTT broker. OSC paths:

```
/rf/river/fft/[band]      — frequency band summary, 500ms or faster
/rf/river/pressure        — barometric pressure, 10–20Hz
/rf/river/temp            — temperature, 1Hz
/rf/river/humidity        — humidity, 1Hz
/rf/forest/[same]
/rf/tent/fft/[band]       — continuous (wired)
/rf/tent/pressure
/rf/tent/temp
/rf/tent/humidity
```

**Logging — HTTP→Supabase (existing stack):**
Temp/humidity/pressure posted every 30s. Same FastAPI/Supabase setup already working on bench. Grafana connects directly to Supabase/Postgres — no InfluxDB needed.

**Camera — HTTP:**
JPEG snapshot every 30s to FastAPI endpoint or directly to Supabase storage.

FFT on-device confirmed: ESP32 computes frequency band summaries from INMP441 input, transmits compact OSC payload. Raw audio never leaves the station.

---

## Signal Router (OSCtopus)

### Current state
- Node.js HTTPS server on Raspberry Pi 3B
- WebSocket routing, OSC UDP on port 5005
- Browser UI at rf.postoccupancy.com
- routing.json for sensor config
- Public repo: https://github.com/residentfrequency/signal-router

### Planned additions for Electric Sky

**OSC/UDP ingestion from ESP32 stations** *(primary new development)*  
ESP32 stations send OSC packets directly to Pi on port 5005 — same protocol OSCtopus already handles. Each station IP must be added to the OSC whitelist (`osc_toggle_send`). Station paths like `/rf/river/fft/low` appear automatically in the router UI. No MQTT broker required.

**Spatial audio routing**  
Each station's audio as separate channel with independent pan. River → rear left, forest → rear right, tent interior → center. Operator selects which station feeds which composition parameter.

**Signal processing primitives** (Tom Henderson's proposal, already partially implemented)  
Slew, attenuation, inversion, offset between raw sensor values and composition parameters. Router already handles min/max normalization and CC assignment. Additional smoothing discussed.

**Attractor state logic** *(discussed, not yet implemented)*  
System computes state from accumulated sensor readings (blob size, rate of change, temperature, occupancy) and transitions composition toward target state with memory/decay. No implementation decisions made.

**Open:** routing.json export/import and GitHub Pages demo/blog post (flagged in May 14 handoff, still pending).

### Multi-station router architecture
The router is designed to accept signals from multiple sources simultaneously. The `source` field on each WebSocket broadcast identifies which IP the signal came from. Multiple sensing stations appear as separate sections in the router UI, grouped by IP. Each station IP needs to be added to the OSC whitelist (`osc_toggle_send`) before it can send to port 5005. No schema changes needed — a station sending `/sensor/river/temp_c 23.4` appears as `osc/river/temp_c` in the router UI automatically.

### Direct UDP path
Any OSC-capable application can bypass the browser router and send UDP directly on the venue LAN. Useful for Tom's shaders and other non-browser tools. Tools: OSCilloscope, OSC Data Monitor, Protokol. Router value-add: MIDI support, normalization controls, browser/p5.js accessibility.

---

## Visual and Projection Layer

### Tent configuration
12x12 hexagonal pop-up canopy tent. Three simultaneous projection surfaces. Projector armatures and surface material (hanging fabric vs. tent walls) — **not yet resolved, Kevin owns this decision.**

Kevin's 3D model (from AV thread): 3 projectors on 3 alternate walls of hexagonal tent, 0.8:1 lens, 10' throw, projection visible from inside and outside.

### Contributors and responsibilities

**Adrian** — p5.js Möiré/interference pattern generator at rf.postoccupancy.com/moire. Currently functional, responding to sensor/MIDI input. Assessed as a starting point but not yet compositionally satisfying. Needs redesign to ground interference patterns in real-world data (FFT of temperature cycles) rather than arbitrary geometric oscillation.

**Tom Henderson** — Generative shader work, forest and river visual themes. Needs from router: specific channel names, value ranges, update frequency. Committed to baked animation demos by June 13–14.

**Kevin Landesman** — Theatrical projection and lighting, 20 years experience. Owns projector rigging and tent surface decisions. Needs: confirmed tent dimensions, power situation, projector specs from Evan.

**Evan Chakroff** — Brings 3 projectors (varying brightness), 5.1 speaker system. Duration of Electric Sky attendance uncertain. Monks/LiDAR piece is separate (VENT!).

### Visual aesthetic direction
- Reference: Saul Bass, Herbert Matter, Josef Albers — high contrast, limited palette, geometric, color doing structural work not decorative work
- Reference: Ryoji Ikeda, UVA (United Visual Artists) — same data rendered simultaneously in audio and visual, not one illustrating the other
- Concept: large geometric forms that scale/translate/subdivide based on system state; color field shifts driven by temperature differential
- Powers of 10 / emergent properties across scales — FFT of long-term temperature data as standing waves perturbed by live readings
- **Rejected:** particle systems, rainbow gradients, fluid simulations, iTunes-screensaver aesthetics

### Scientific data display
Dedicated surface showing raw sensor feeds, time series, and data visualizations as permanent ground-truth layer — always on, separate from interpretive compositions.

**Open:** Whether Tom's shaders and Adrian's p5 sketch need shared data schema or can define independent parameter mappings.

### Gabor limit as live performance parameter

FFT window size is not a fixed implementation detail — it is a controllable parameter that determines which features of the audio signal are legible:

- **Short window** → better time resolution, worse frequency resolution. Transients, rhythmic events, and percussive impacts are visible. Pitch and tonal content blur.
- **Long window** → better frequency resolution, worse time resolution. Harmonic content, pitch, and tonal relationships are visible. Temporal events smear.

This tradeoff (the Gabor limit — the signal-processing analogue of Heisenberg uncertainty) is worth making explicit as a live control. A MIDI knob or slider mapped to FFT window size lets the operator rotate in time-frequency space in real time — trading temporal resolution for spectral resolution. At one extreme the system hears rhythm; at the other it hears harmony. Same audio signal, different physics.

**This is not just a visualization zoom.** Changing window size changes what compositional parameters are extractable. Different window sizes feed fundamentally different signals into the router.

**Visualization of the uncertainty:** as the knob moves, display both axes simultaneously degrading and improving — explicit visual representation of the tradeoff. This makes the Gabor limit a readable, performable element of the piece rather than a hidden implementation detail.

**Connection to temporal scale concept:** the FFT window knob is a "which timescale am I resolving?" selector — the same question the inference cadence logic answers at longer timescales. They are the same idea operating at musical (millisecond) vs. ecological (minute/hour) resolution.

**Implementation:** window size as a parameter in the ESP32 firmware FFT routine, controllable via OSC message from the router. Router exposes it as a mappable MIDI input.

---

## Open Questions Summary

| Question | Priority |
|---|---|
| Reply to Shelly to accept grant | Urgent |
| Join Discord before July 2 | Urgent |
| Reply to Kevin re: tent layout and projector specs | This week |
| Reply to AV thread generally | This week |
| GL.iNet AP vs. Pi AP mode | Before hardware order |
| Battery voltage monitoring / fallback behavior | Before build |
| Projector mounting and surfaces | Kevin owns |
| p5.js visual redesign (grounded in FFT data) | Before July |
| Kevin Landesman free ticket — clarify with Shelly | With Shelly reply |
| Tent: buy vs. borrow from Shelly | With Shelly reply |
| Tom's aspect ratio question for Electric Sky tent walls | After tent confirmed |

---

## Phase 2: Local Inference Server (Post-Electric Sky)

*Out of scope for Electric Sky 2026. Documented here as the next hardware build.*

### Concept
A dedicated machine running Ollama serves as a temporal-scale perception layer — operating on sensor history at cadences inaccessible to human attention (30s, 5min, hourly) and returning structured attractor state assessments that feed back into the composition via OSCtopus. Different inference cadences correspond to different temporal scales in the system, each modulating different composition parameters. This is the "emergent properties across scales" concept instantiated as infrastructure.

### Inference cadence design
| Cadence | Temporal scale | What it perceives | Feeds |
|---|---|---|---|
| 30s | Crowd dynamics | Energy shifts, pressure events | Tension/release parameters |
| 5–10 min | Set arc | Building/waning, temperature trends | Structure, arrangement density |
| Hourly | Day/night, weather | Diel cycle, pressure systems | Tonal palette, timbral baseline |

### Hardware BOM

| Part | Qty | ~Price | Source | Notes |
|---|---|---|---|---|
| RTX 3060 12GB | 1 | $150–200 | Facebook Marketplace / eBay | 12GB VRAM fits 7–8B model at Q8 with headroom; more VRAM than 3070/3080 |
| Motherboard — AM4 (B450/B550) or LGA1200 (B460/B560) | 1 | $50–80 | Marketplace / eBay | Any PCIe x16 slot; AM4 preferred for CPU upgrade path |
| CPU — Ryzen 5 3600 or Intel i5-10th gen | 1 | $40–60 | Marketplace / eBay | Inference is GPU-bound; CPU is near-idle during generation |
| RAM — 16GB DDR4 (2× 8GB) | 1 set | $25–35 | Marketplace / eBay | 32GB preferred if available at similar price |
| SSD — 256GB+ NVMe or SATA | 1 | $35–45 | Amazon (new) | OS + models; 7–8B model at Q4 ≈ 5GB, leave room for 3–4 models |
| PSU — 550W 80+ Bronze | 1 | $45–55 | Amazon (new) | Buy new — used PSUs are a reliability risk; RTX 3060 needs ~170W |
| Case — budget ATX mid-tower | 1 | $30–40 | Marketplace or new | Airflow matters for festival use; avoid tiny ITX |

**Total: ~$375–515**

**Alternative:** A used gaming PC sourced as a unit from Facebook Marketplace (search RTX 3060, 16GB RAM) often comes in at $350–450 all-in — frequently cheaper and faster than sourcing parts individually. Inspect PSU age.

### Software (all free)
- Ubuntu Server 22.04 LTS
- Ollama
- Starting models: `llama3.1:8b-instruct-q4_K_M` (~4.7GB), `mistral:7b-instruct-q4_K_M` (~4.1GB)

### Performance
RTX 3060 12GB at Q4_K_M: ~50–70 tokens/sec. A 1500-token inference completes in ~20–25 seconds — within 30s cadence and well within 60s. No fine-tuning required for structured state classification; prompt engineering sufficient for first iteration.

### Dual-purpose value
A used gaming PC (RTX 3060, 16GB RAM) serving as kids' gaming computer at home doubles as the festival tent workstation — running visuals (TouchDesigner, p5.js), Ollama inference, and optionally OSCtopus if the Pi isn't present. Same GPU handles LLM inference and real-time data visualization rendering. Node.js (OSCtopus) runs on Windows. Pi remains as always-on home server and festival fallback at 5W; gaming PC is the high-compute node when present.

**Tent power note:** For grid-powered festivals (Electric Sky is grid-powered), the gaming PC is straightforward. Projectors are the dominant tent load (200–400W each × 3 = 600–1200W); the PC adds 200–300W. Total tent draw 1500–3000W — requires festival grid power, not portable solar.

---

## Phase 3: Solar-Powered Full Installation

*Longer-term concept. Documented here while the architectural thinking is fresh.*

### The constraint that becomes the concept

For a fully solar-powered installation, the RTX 3060 gaming PC is the wrong inference hardware — 200-300W under load is expensive against a solar budget. The alternative:

**Mac Mini M-series** — 8–20W under load. Unified memory (16GB) acts as both RAM and VRAM; runs 13B models via MLX at 20–40 tokens/sec. Inference in under 30 seconds at ~1/15th the power draw of the gaming PC. A 200W panel and modest LiFePO4 battery sustains it indefinitely. Used M1 Mac Mini ~$600; M4 new ~$800.

### Inference cadence as power budget

On solar, inference cadence becomes an explicit design parameter rather than a cost optimization. The system models:
- Available watt-hours from panel (varies with cloud cover, season, time of day)
- Fixed baseline draw: Pi, router, sensing stations
- Variable draw: each inference cycle costs X watt-seconds
- Maximum cadence that keeps the battery healthy overnight

On a cloudy day the system runs inference less frequently. On a clear day it thinks faster. **This is not a bug — it is the system being literally powered by the same environmental conditions it is sensing.** The temporal resolution of the attractor state logic scales with available solar energy. Sparse inference during overcast weather; dense inference during bright sun. The environment modulates its own perception of itself. **The INA219 charging current stream is the data feed the inference cadence scheduler reads** — solar irradiance derived from charge current, streamed via OSC/UDP from each station.

### Projector power
Lamp projectors (200–400W each) remain the dominant load for any solar tent installation and require a serious array + battery bank (10–20kWh, $3000–8000+). LED projectors (50–100W each) make solar tent projection feasible at much lower infrastructure cost. Projector selection is the highest-leverage decision for a solar installation.

### Open questions (Phase 3)
- Fine-tuning on project-specific aesthetic vocabulary vs. prompt engineering only
- Whether 13B model meaningfully improves attractor state reasoning over 8B for this task
- Integration point: Ollama output → OSCtopus as OSC message vs. direct WebSocket injection
- LED projector candidates that meet brightness requirements for outdoor/ambient light conditions
- Battery bank sizing for multi-day deployment without grid backup

---

## Live Infrastructure (Pre-Electric Sky)

These are working systems already deployed, not Electric Sky deliverables:

**AM2320 temperature/humidity sensor** — ESP32 bench sensor sending to Supabase via HTTP over WiFi, polled by the signal router. Live at rf.postoccupancy.com. Mains-powered, not suitable for solar deployment as-is, but firmware is the reference for the outdoor station build.

**HC-SR04 ultrasonic distance sensor** — On Pi GPIO pins 17/27, toggle button on GPIO 22. Working. Indoor range sensor, available as MIDI controller input in the tent.

**TF-Luna LIDAR** — Appeared in router signal table (`TF-Luna/ch1/cc1`) at some point, suggesting it was connected and working. No further integration documented. (LiDAR excluded from Electric Sky observatory tent — reserved for Evan's VENT! monks piece.)

**SuperCollider (sendToSC)** — server.js has a `sendToSC` function sending OSC to localhost:57110. Currently unused/optional. Available if anyone wants to route signal into SC for audio processing.

**Known pending router UI issues:**
- routing.json export/import button not yet implemented
- Moire color preview sliders don't move when CC changes color (visual desync only — mapping works)
- Blog post and demo video for GitHub Pages still pending

---

## Prior Work / Context

- **Electric SEA hackathon** (March 2026): First iteration of OSCtopus, small gallery space, confirmed signal chain
- **PodQuadGeddon at Art+Tech Symposium**: Generative audio, ocean recordings from Salish Sea, evolving spatial experience
- **Orcasound.net**: Adrian's TypeScript/Python/UX contributions to open source marine acoustic monitoring (now transitioning away, building own platform)
- **VENT! at Georgetown Steam Plant** (June 28): Evan's separate piece using LIDAR precision mapping + Kinect for monks/choir room experience. Adrian supports with signal router. Test run for some visual components.

---

*This document is the living working spec for Resident Frequency at Electric Sky 2026. Compiled from two claude.ai thread handoffs (June 2026) and grant/festival correspondence. Update after June 20 collaborator meeting.*
