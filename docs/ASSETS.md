# Third-party assets used in the build

Everything below is referenced from `Config/DefaultGame.ini`, section `[/Script/HypeCubeJam.HCJAssetSettings]`
(class `UHCJAssetSettings`). Nothing else from the packs is cooked into the packaged game.

| Asset | Author | Source | Licence | Used for | In project |
|---|---|---|---|---|---|
| Spaceship Modular Star Sparrow | Ebal Studios | Fab | Fab Standard License | ship hulls (40 variants), 10 colour skins | `/Game/StarSparrow` (imported from `SourceAssets/StarSparrow`) |
| Icelandic Boulder (tazj1), Icelandic Boulder (tdpsdbcda), Icelandic Porous Rocks (skxqf), Nordic Beach Boulder (venjcjyqx) | Quixel Megascans | Fab | Fab Standard License | asteroids | `/Game/Megascans/Asteroids` (imported from `SourceAssets/Megascans`) |
| Space Station 4 | Gerardo Justel | Fab (`SourceAssets/SpaceStation4/space_station_4.glb`, 155 MB) | CC-BY: the credit "Space Station 4 by Gerardo Justel" is shown in the game (SETTINGS page), in `README.txt`, `NOTICES.txt` and the project README | the look of the space stations (their collision stays the game's own boxes) | `/Game/SpaceStation4`: one mesh `SM_Station4` (98 914 triangles, 3 sections, 4 LODs: 98 914 / 44 496 / 17 799 / 5 933; no Nanite, no collision), 8 textures, material `M_HCJ_Station` + 2 instances, `M_HCJ_StationLights`. Changed from the source: the 20 meshes merged into one with their transforms baked in; the nine 4096 maps reduced to eight 2048 maps (the clear-coat normal map dropped; in the game base colour and emission are limited to 2048, ORM and normal map to 1024); the model's glTF materials (clear coat) replaced by a plain lit material; of the "Emission" mesh - mostly a cloud of loose lights reaching 20 m from a 10 m hull - only the 2 299 of 13 556 triangles within 0.6 m of the hull are kept. Scripts: `docs/tools/prep_station4.py` (plain Python + Pillow), `import_station4.py` (Interchange glTF, editor commandlet), `station4_lods.py` (editor) |
| Intro video `intro_veo.mp4` (1280 x 720, 10 s, H.264) | generated with Google Veo (Gemini) by the project's author | generated, not from a store (`SourceAssets/Veo`) | AI-generated; no third-party licence text. Check the terms of the Gemini / Veo service that apply to the account before publishing | played once before the title menu, without its sound track | `Content/Movies/intro_veo.mp4` (a loose file next to the pak, not an asset) |
| Lyra Starter Game | Epic Games | Fab | Unreal Engine content (Fab / Unreal Engine EULA) | 47 sound waves: music stems (`mx_v1_combat_a_*`, `mx_menu_*`), weapon punches, impacts, explosion punches, portal / dash / pickup / ring-loop effects, kill / spawn / end-of-round cues | `/Game/Audio` (copied from the launcher vault with their original paths; sound class, looping and virtualisation settings changed by `docs/tools/setup_audio.py`) |
| "Starward Bound", "Gravity Well Hunt", "Stellar Breach" (three music tracks) | Music generated with Google Lyria (Gemini) by the project's author, 14 Nov 2026 | generated, not from a store | AI-generated; no third-party licence text. Check the terms of the Gemini / Lyria service that apply to the account before publishing | the match music (set 0): title menu, exploring, combat | `/Game/Audio/Lyria` (imported from `SourceAssets/Lyria/*.mp3` by `docs/tools/import_lyria.py`: the editor's sound factory reads MP3 itself) |
| Sci-Fi Modular Hover Vehicles (hover tank pack) | as listed on the pack's Fab page | Fab | Fab Standard License | missile `SM_hovertank_missile_one`, turret head and gun `SM_hovertank_head`, `SM_hovertank_gun_large`, bolt mesh `SM_ProjectileLong`, glow material `MAT_Lights` (portals, bolts), engine loop `CUE_Noise` (wave `SND_Kenney_spaceEngineLarge_001`), explosion sounds `CUE_Explosion_Random` | `/Game/HoverVehicles` |
| Niagara Examples | Epic Games | Fab | Fab Standard License / Unreal Engine content | muzzle flash, bolt trail, boost trail, impacts, ship explosion (`NS_MuzzleFlash`, `NS_SimpleRibbonTrail`, `NS_RocketTrail`, `NS_Impact_Metal`, `NS_Impact_Concrete`, `NS_Explosion_Small`) | `/Game/NiagaraExamples` |
| CelestialVault engine plugin content: HYG star catalogue (`DT_HYGCatalog_10K`), `T_MilkyWay`, Solar System planet table and atlas, Moon textures and material, star material, dome and card meshes | Epic Games (Unreal Engine 5.9 plugin); the HYG database is by David Nash / astronexus | ships with Unreal Engine | Unreal Engine EULA (HYG database: CC BY-SA 4.0) | stars, Milky Way, planets, Moon | `/CelestialVault` (engine plugin, not copied) |
| Engine basic shapes and materials | Epic Games | ships with Unreal Engine | Unreal Engine EULA | all collision shapes, portal geometry, planet sphere, fallback look | `/Engine/BasicShapes` |

Made for this project (no third-party content): sound classes and submixes `/Game/HCJ/Audio/SC_HCJ_{Music,SFX,UI}`,
`SM_HCJ_{Music,SFX,UI}` (built by `docs/tools/setup_audio.py`), materials `/Game/HCJ/Backdrop/M_HCJ_BlackHole`, `M_HCJ_Nebula`,
`M_HCJ_Planet` (procedural shaders, built by script), `M_HCJ_PlanetSurface`, `M_HCJ_PlanetRing`
(`docs/tools/make_planet_materials.py`; the surface samples the CelestialVault planet atlas), `/Game/HCJ/Effects/M_HCJ_ShieldBubble`
(shield bubble, built by `docs/tools/make_shield_material.py`), `M_HCJ_ShipRim`, `M_HCJ_ShipGlint`, `M_HCJ_EngineTrail`
(`make_ship_fx_materials.py`), `M_HCJ_PortalSwirl` (`make_portal_materials.py`), the torpedo, phaser and flare materials, `/Game/StarSparrow/Materials/M_StarSparrow` and its
instances, `/Game/Megascans/Asteroids/Materials/M_Asteroid` and its instances.

Not used in the build although present on disk: the rest of the Hover Vehicles and Niagara Examples packs,
`SourceAssets/SciFiSpaceStation` ("Sci-Fi Space Station" by Helindu.Art, Fab, CC-BY). It is NOT in the build and
not in `Content/`: a test import gave one mesh of 2.1 million triangles with 49 untextured material slots; it was
rejected for that, and the stations are now drawn with "Space Station 4" by Gerardo Justel (table above) instead.
If the Helindu.Art model is ever used, its CC-BY licence requires the credit "Sci-Fi Space Station by Helindu.Art"
in the game's credits.

Notes
- The Hover Vehicles pack is listed in the local Fab library database as "Sci-Fi Modular Hover Vehicles"; its author
  name and the exact licence line of each Fab listing are not stored on disk; fill them in from the Fab pages
  before publishing.
- Lyra: only the sound waves the game plays were copied (22.8 MB of source assets); none of Lyra's MetaSounds,
  sound classes, submixes or code is used.
- Lyria tracks, measured from the imported waves (`docs/tools/music_stats.py` on the WAVs the import script exports):
  all three 44.1 kHz stereo, peak 0.0 / 0.0 / -0.1 dBFS, RMS -14.3 dBFS each; 167.7 s (combat), 175.6 s (explore),
  168.6 s (menu). The Lyra stems are far quieter: -26.6 dBFS (bass, percussion) to -48.5 dBFS (pad) per file, about
  -30 dBFS for the calm mix (pad + bass at their layer volumes) and -25.5 dBFS with all six layers. The tracks
  therefore play at a gain of 0.30 / 0.26 / 0.32 (menu / explore / combat: -10.5 / -11.7 / -9.9 dB, i.e. about
  -24.8 / -26.0 / -24.2 dBFS RMS) so that the music slider gives about the loudness it gave with the stems.
  Loop points: every track starts at full level and ends in an outro (menu: fading from 157 s; explore: quiet
  from 157.6 s to its end; combat: stops at 164.7 s, then 2.6 s of silence), so the ends do not match and the game
  loops them itself: the next pass starts at 157 s / 157 s / 163 s and is cross-faded over 4 / 4 / 1.5 s.
  Nobody has listened to them in the game.
- New materials `M_HCJ_PhaserBeam` and `M_HCJ_ExplosionFlare` (`/Game/HCJ/Effects`) are procedural, built by
  `docs/tools/make_phaser_fx_materials.py`; the phaser sounds, the debris meshes and the particles of a breaking
  rock are assets that were already in the project (Lyra ring loop / dash / item-replenish, the Hover Vehicles
  noise loop and explosion, the Megascans rocks, the engine cube, Niagara Examples). Nothing else was imported.
- The Kenney engine sound inside the Hover Vehicles pack is, by its file name, from Kenney (kenney.nl, CC0).
