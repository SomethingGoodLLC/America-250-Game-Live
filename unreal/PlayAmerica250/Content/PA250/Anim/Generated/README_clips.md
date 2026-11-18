# Generated clips: gun drill, volley, reactions

Offline-generated animation for the soldier (`SK_AMERICAN_SOLDIER_Skeleton`). Each clip `AM_<Clip>` has variants
`AM_<Clip>_v0 .. _v<N-1>` in `Gun/`, `Volley/` or `React/`. 30 fps. Made with Kimodo `Kimodo-SOMA-RP-v1` (text + keypose
constraints), retargeted through `/Game/PA250/Anim/Rig/RTG_SOMA_to_Soldier`; seeds, prompts and constraints are in
`manifest.json` in the AnimationPass source pack (outside the repository). Tools: `tools/anim`.

Blast suffixes: `_F` blast in front (thrown backward), `_B` behind (thrown forward), `_L` on his left (thrown
right), `_R` on his right (thrown left). Notifies are Blueprint notify classes in `Notifies/`, on track `PA250`.
Hands = the clip's hands were pinned to gun targets (baked). Any variant of a clip may follow any variant of the
clip before it in a chain; where the join is not direct, a transition clip below bridges it.

## Gun

| Clip | Variants | Seconds | Root motion | Loop | Hands | Chains to | Notifies |
|---|---|---|---|---|---|---|---|
| `AM_Gun_Fire` | 3 | 2.5 | no | no | no |  | Notify_GunStep_Done |
| `AM_Gun_Flinch` | 4 | 1.5 | no | no | no |  | Notify_FootPlant |
| `AM_Gun_Idle_Captain` | 3 | 4.0 | no | yes | no |  |  |
| `AM_Gun_Idle_Firer` | 3 | 4.0 | no | yes | no |  |  |
| `AM_Gun_Idle_Handspike` | 3 | 4.0 | no | yes | no |  |  |
| `AM_Gun_Idle_Loader` | 3 | 4.0 | no | yes | no |  | Notify_FootPlant |
| `AM_Gun_Idle_Sponger` | 3 | 4.0 | no | yes | no |  |  |
| `AM_Gun_Idle_Ventsman` | 3 | 4.0 | no | yes | no |  |  |
| `AM_Gun_Lay` | 3 | 3.0 | no | no | no |  | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_Load` | 3 | 3.0 | no | no | yes |  | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_Observe` | 2 | 4.0 | no | no | yes |  |  |
| `AM_Gun_Prime` | 2 | 3.0 | no | no | yes |  | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_Ram` | 3 | 3.0 | no | no | yes |  | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_RunUp` | 2 | 3.0 | yes | no | no |  | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_Sponge` | 3 | 4.0 | no | no | yes | `AM_Gun_Ram` | Notify_FootPlant, Notify_GunStep_Done |
| `AM_Gun_Traverse` | 2 | 3.0 | no | no | no |  | Notify_GunStep_Done |
| `AM_Gun_Vent` | 2 | 4.0 | no | no | yes | `AM_Gun_Prime` | Notify_FootPlant, Notify_GunStep_Done |

## Volley

| Clip | Variants | Seconds | Root motion | Loop | Hands | Chains to | Notifies |
|---|---|---|---|---|---|---|---|
| `AM_Reload_PrimePan` | 3 | 3.0 | no | no | no | `AM_Reload_RamCharge` | Notify_FootPlant, Notify_Reload_PhaseEnd |
| `AM_Reload_RamCharge` | 3 | 4.0 | no | no | no | `AM_Reload_Shoulder` | Notify_FootPlant, Notify_Reload_PhaseEnd |
| `AM_Reload_Shoulder` | 3 | 2.0 | no | no | no |  | Notify_FootPlant, Notify_Reload_PhaseEnd |
| `AM_Volley_Fire` | 4 | 1.5 | no | no | yes |  | Notify_FootPlant, Notify_Volley_Discharge |
| `AM_Volley_Kneel_Fire` | 3 | 1.5 | no | no | yes |  | Notify_FootPlant, Notify_Volley_Discharge |
| `AM_Volley_Kneel_MakeReady` | 3 | 2.0 | no | no | yes | `AM_Volley_Kneel_Present` |  |
| `AM_Volley_Kneel_Present` | 3 | 1.5 | no | no | yes | `AM_Volley_Kneel_Fire` | Notify_FootPlant |
| `AM_Volley_MakeReady` | 4 | 2.0 | no | no | yes | `AM_Volley_Present` |  |
| `AM_Volley_Present` | 4 | 1.5 | no | no | yes | `AM_Volley_Fire` | Notify_FootPlant |

## React

| Clip | Variants | Seconds | Root motion | Loop | Hands | Chains to | Notifies |
|---|---|---|---|---|---|---|---|
| `AM_Death_Shot_B` | 3 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_Death_Shot_F` | 3 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_GetUp_Back` | 2 | 3.0 | yes | no | no |  | Notify_FootPlant |
| `AM_GetUp_Front` | 2 | 3.0 | yes | no | no |  | Notify_FootPlant |
| `AM_GetUp_Side` | 2 | 3.0 | yes | no | no |  | Notify_FootPlant |
| `AM_React_BlastKnockdown_B` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant, Notify_Ragdoll_Handoff |
| `AM_React_BlastKnockdown_F` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant, Notify_Ragdoll_Handoff |
| `AM_React_BlastKnockdown_L` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant, Notify_Ragdoll_Handoff |
| `AM_React_BlastKnockdown_R` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant, Notify_Ragdoll_Handoff |
| `AM_React_BlastStagger_B` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_React_BlastStagger_F` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_React_BlastStagger_L` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_React_BlastStagger_R` | 2 | 2.0 | yes | no | no |  | Notify_FootPlant |
| `AM_React_Deafened` | 2 | 3.0 | no | no | no |  |  |
| `AM_React_DirtSpray` | 3 | 1.5 | yes | no | no |  | Notify_FootPlant |
| `AM_React_NearMiss` | 4 | 1.5 | no | no | no |  | Notify_FootPlant |
| `AM_Wound_Crawl` | 2 | 3.0 | yes | no | no |  | Notify_FootPlant |
| `AM_Wound_Leg_Fall` | 3 | 2.5 | yes | no | no |  | Notify_FootPlant |

## Transitions (MotionBricks in-betweening, conditioned only on the clips above)

Named `AM_<X>_to_<Y>_v<a>_v<b>`: from variant a of X to variant b of Y. No notifies, no root motion.
Ragdoll joins are in the knockdown's space: the get-up that follows must be placed where the transition ends.

| From | To | Clips | Frames |
|---|---|---|---|
| `AM_React_BlastKnockdown_B` | `AM_GetUp_Front` | 4 | 42 to 50 |
| `AM_React_BlastKnockdown_F` | `AM_GetUp_Back` | 4 | 26 to 50 |
| `AM_React_BlastKnockdown_L` | `AM_GetUp_Side` | 4 | 26 to 50 |
| `AM_React_BlastKnockdown_R` | `AM_GetUp_Side` | 4 | 50 to 50 |
| `AM_Reload_RamCharge` | `AM_Reload_Shoulder` | 3 | 26 to 30 |
| `AM_Volley_Kneel_MakeReady` | `AM_Volley_Kneel_Present` | 9 | 26 to 26 |
| `AM_Volley_Kneel_Present` | `AM_Volley_Kneel_Fire` | 3 | 26 to 26 |
| `AM_Volley_MakeReady` | `AM_Volley_Present` | 3 | 26 to 26 |
