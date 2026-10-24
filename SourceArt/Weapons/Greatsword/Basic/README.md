# Greatsword Basic source artwork

Shared production standard: [Weapon mesh and texture standards](../../../../Docs/Combat/Authoring/Weapon_Asset_Standards.md). This file retains the asset-specific import and socket recipe.

The three FBX files and three 2048×2048 textures are the supplied GothicGreatsword_MMO game-ready output, preserved byte-for-byte. The editable Blender original remains with the supplied artwork; OpenGL normals and preview renders are not runtime inputs. `SHA256SUMS.txt` identifies the preserved inputs. The original exporter notes and socket validation are retained alongside this integration recipe.

Runtime asset: `/Game/Weapons/Greatsword/Basic/SM_Greatsword_Basic`. Visual Blueprint: `/Game/Character_BPs/GreatSword/BP_Greatsword_Basic`, a direct child of `Project_JWeaponPresentationActor`. The existing starter item and presentation profile select this Blueprint; item identity, abilities, combat style, montages, retarget/body calibration and skeletal attachment sockets remain unchanged.

Import with the legacy FBX static-mesh importer, translation/rotation zero and uniform scale 1; Convert Scene on, Convert Scene Unit and Force Front X Axis off; Transform Vertex to Absolute on and Bake Pivot in Vertex off. Disable material/texture auto-import and collision generation. Use **Import Normals and Tangents** for all LODs, retaining the supplied tangent basis. Recomputing Mikk tangents on this supplied mesh produced near-zero tangent/binormal warnings. Final imported LODs use the FBX tangent basis without those warnings. Nanite is disabled; this movable cosmetic mesh does not generate lightmap UVs.

LOD0/1/2 have 14,980 / 7,490 / 3,744 triangles and one material slot. Screen sizes are 1 / 0.35 / 0.18. Both local and remote weapons use engine screen-size LOD selection; possession no longer pins local weapons to LOD0. Authored component forced-LOD overrides remain available. Body/clothing LOD policy is separate and unchanged.

**Custom LOD import caveat:** the supplied LOD1/2 FBXs contain no sockets. Unreal's legacy custom LOD import removes sockets flagged as import-created when the LOD source omits them. After importing all LODs, restore these four mesh-local sockets as editor-authored sockets and verify them again after future reimports. All rotations are zero, all scales are one:

| Socket | Location (cm) |
| --- | --- |
| WeaponGrip_R | 0, -2, 0 |
| WeaponGrip_L | 0, 6, 0 |
| WeaponGroundProbe_Tip | 18.5, -145, 0 |
| WeaponGroundProbe_Base | 0, -191.3, 0 |

These match the existing `SM_Sword` presentation contract. Do not compensate by changing character skeleton sockets or Blueprint component transforms.

`M_Greatsword_Basic` uses BaseColor (sRGB), DirectX Normal (normal-map compression, linear, no green-channel flip), and ORM (mask compression, linear; R=AO, G=Roughness, B=Metallic). BaseColor/Normal have a maximum built size of 2048; ORM has a maximum of 1024. The original 2K ORM source remains intact, so this cap is reversible. All three use mipmaps and texture streaming, with Weapon / WeaponNormalMap / WeaponSpecular texture groups respectively. Do not duplicate these textures per local/remote actor or force all mips resident. The cap reduces ORM's maximum texel area by 75%; total VRAM and frame time depend on compression, resident mips, visible assets and other rendering costs.

The native actor retains its no-collision, no-overlap, no-navigation, no-tick and no-transform-replication responsibilities. Existing dynamic shadows are retained; this integration adds no arbitrary distance cull, per-weapon tick, streaming manager or server-side cosmetic actor.
