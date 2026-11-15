# MotionBricks evaluation results

## Unreal and source archive

Tested on Windows with Unreal **5.8.4**, CL 58902052, MSVC 14.44.35230, Windows SDK 10.0.22621 and .NET Framework SDK 4.8. The NVIDIA L4 has 23,034 MiB reported framebuffer, driver 596.86. Unreal Build Accelerator reported 30.8 GiB physical RAM.

The supplied ZIP extraction has no Git metadata. It contains 1,072 uassets and eight maps. Of those, 338 uassets and four maps are Git LFS pointer text. The full Quinn mannequin subtree contains binary packages. The licensed `/Game/american_soldier` and weapon pack were unavailable in the archive. The prototype uses Quinn and evaluates unarmed traversal.

The original game build reached C++ compilation after the Windows development tools were installed. It failed on pre-existing errors in PA250Sim tests (`dirent.h`), the shader module (missing `PA250Shaders.h`), and MSVC C4458 member shadowing in battle, province map and RF character code. The original editor launch reported an unbuilt PA250Shaders module. The test-host build used the same engine with the existing mannequin assets and the new plugin, and compiled separately. The Unreal prerequisite check emitted an outdated Visual C++ redistributable error, but the commandlet saved all three packages and the rendered game ran.

## NVIDIA generation

- Upstream revision `54fc563aeaaf819a753d72cc9fdc6713989cdd43`, SOMA FP32 using TensorRT 10.8 with TF32 permitted.
- The stock example ran on CUDA and output chained segments of 26 and 50 frames. SOMA Scene Preview opened. The [computer-use skill](<C:/Users/user/.codex/plugins/cache/openai-bundled/computer-use/26.1111.21308/skills/computer-use/SKILL.md>) uses app-scoped Windows controls. The preview capture still returned `Computer Use was not approved to use python` after approval. Interactive preview behavior remains unverified.
- The 0.5 m comparison clip produced 38 finite frames in 1.233 s. The selected 0.75 m `OneHandedJump0` clip produced 41 finite frames over 1.333 s, with max quaternion norm error `2.38e-7`. The output hash was `7ec5e7356b485cf30df05a836966771ebf16379920692b43027535b0a9798488`.
- During the warm benchmark the GPU showed 1,402 MiB in use after CUDA context initialization and 2,170 MiB with the engine loaded, a device-wide difference of 768 MiB. These are snapshots and include any other GPU activity. Under WDDM they do not establish process-exclusive peak VRAM. The separate running preview process showed approximately 4.0 GiB resident and 7.9 GiB peak working set. The TensorRT plan was about 731 MiB on disk.

| Timed operation | p50 | p95 | Sample |
| --- | ---: | ---: | --- |
| SOMA model GPU inference | 7.52 ms | 8.09 ms | 30 warm batch-one runs |
| Encode, infer, decode and synchronize | 8.43 ms | 9.18 ms | Same 30 runs, measured on CPU |
| Setup with existing TensorRT cache | 1.41 s | — | One observed run |
| First generated segment in new process | 79.08 ms | — | Includes CUDA graph setup and library warm-up |
| Second segment in same process | 9.20 ms | — | Includes encode, infer, decode and synchronization |

The warm timing excludes startup and disk export. The prototype does offline inference, so these times are outside the Unreal frame.

## Import and rendered gameplay

The editor wrote a 41-frame `UAnimSequence`, a clip data asset and `L_MotionBricksTraversal`. Quinn has 89 bones. Import scale was 0.97518, making the marked start 172.47 cm from the obstacle center and the course obstacle about 76 cm tall. The generated timing is 30 Hz. The rendered 1280×720 Direct3D 12 tests loaded those saved assets in a fresh game process. The first successful attempt used a simulated **E** press and release through Unreal's player controller, reaching the game's real key binding. Other scenarios call the same traversal method directly.

| Check | Final playback |
| --- | --- |
| Grounded, correctly aligned start | Accepted |
| Excessive distance and wrong heading | Both rejected |
| Obstacle in landing volume | Rejected before playback |
| Repeat during playback | Rejected without interrupting playback |
| Two consecutive complete attempts | Passed at 30 and 60 FPS caps |
| Animation and capsule synchronize | Montage position matched the capsule clock within 1 ms at the sampled frame. Slot weight was 1.000 at both caps |
| Return to walking | Standing capsule restored and pawn moved over 40 cm after the landing |
| Planar displacement relative to root curve | 0.000 cm drift at both caps |
| Game frame delta at 30 FPS cap | p50 33.33 ms, p95 33.33 ms |
| Game frame delta at 60 FPS cap | p50 16.67 ms, p95 16.67 ms, 150 samples |

These frame deltas include the engine's frame cap and do not isolate animation CPU time. The screenshots cover the marked approach, hand reach, clearance and first landing. The earlier montage import left the mannequin IK goals at their bind pose and pulled the boots toward the obstacle. Baking their tracks to the animated ankles restored visible clearance in the mid-vault still. The palm still hovers above the wall and the first exit still shows a foot off the ground during blend out. The retarget has no foot lock. The SOMA source showed up to 1.95 cm planar toe travel per adjacent frame when both frames had a predicted contact of at least 0.8. This measures the source feet. The reviewed stills cannot rule out brief target mesh intersection or floor sliding between captures.

Local evidence lives in the workspace's `MotionBricksEvaluation` folder. See `VaultTestHost/MBImport_final.log`, `VaultTestHost/MBVerify_30.log`, `VaultTestHost/MBVerify_60.log`, `VaultBenchmark/metrics.json` and `Evidence/VaultFinal_30fps` or `Evidence/VaultFinal_60fps`.

Run the manual walkthrough in SETUP.md for the real keys and observe hands and feet from the side. Weapon grip, coat and bayonet clipping require the licensed soldier and kit. The normal game's campaign map and combat rules were outside the test host.

## Contact polish retest

The updated vault keeps the generated 41-frame sequence and planar trajectory. It adds a left-palm plant during frames 16–19, a left-foot touchdown during frames 32–34, and a planted right foot during frames 38–40. The corrected shoulders and legs preserve bone lengths. The capsule settles vertically before walking resumes.

The importer originally hit the hand anchor in the animation data while rendered playback lifted the hand 4–5 cm. Comparing the same sequence with Quinn selected as the evaluation mesh traced this to the shared skeleton's reference proportions. Setting the retarget source mesh to Quinn restored the contact during playback.

Both Unreal 5.8.4 Direct3D 12 runs passed. The samples below come from the evaluated skeletal mesh after the actor and animation ticks. The maximum errors compare a palm or sole marker with the actual wall or floor height and its anchored point. The final frame is the walking handoff, so the right-foot measurements cover the samples before release.

| Contact | 30 FPS max height / planar error | 60 FPS max height / planar error |
| --- | ---: | ---: |
| Left palm | 0.151 / 0.004 cm, 4 samples | 0.151 / 0.168 cm, 7 samples |
| Left sole | 0.497 / 0.815 cm, 3 samples | 0.497 / 0.815 cm, 5 samples |
| Right sole | 0.500 / 0.813 cm, 2 samples | 0.500 / 0.813 cm, 4 samples |

Both caps passed the existing trigger, rejection, repeat, synchronization, and walking checks. Planar root drift stayed at 0.000 cm. Median frame deltas were 33.333 ms at the 30 FPS cap and 16.667 ms at the 60 FPS cap.

The screenshots show the palm on the top of the wall and a planted left boot during touchdown, followed by the right step. Find all six screenshots and the rendered log for each cap in `MotionBricksEvaluation/Evidence/Contacts_30fps` and `MotionBricksEvaluation/Evidence/Contacts_60fps`. The markers approximate contact surfaces on Quinn. This does not measure every mesh vertex or cover arbitrary obstacles.
