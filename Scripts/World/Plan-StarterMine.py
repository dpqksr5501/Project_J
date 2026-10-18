"""H08's owned, hollow ridge adit; leaves the preserved Landscape untouched."""
import hashlib,importlib.util,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
FILE=SOURCE/'StarterWorld.H08.mine.json'
assert not FILE.exists(),'Do not overwrite a published mine survey'
s=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
T=m.TerrainProfile(SOURCE/'StarterRegion.manifest.json')
cx,cy=2300.,5950.;yaw=140.;co,si=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
def xy(u,v):return cx+co*u-si*v,cy+si*u+co*v
# This narrow natural ridge shelf is not a replacement for the large outdoor
# combat circles. The gallery follows the contour, with an excavated-rock floor
# above the existing Landscape, so no invisible Landscape seals the passage.
terrain=[T.height(*xy(u,v)) for u in range(-8,19) for v in [-4,-2,0,2,4]]
floor=max(terrain)+.18
assert max(terrain)-min(terrain)<1.2
vertices=[];triangles=[];uvs=[]
def face(points):
    a,b,c=points[:3];ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)];n=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]];axis=max(range(3),key=lambda i:abs(n[i]));axes=[i for i in range(3) if i!=axis];start=len(vertices)
    for p in points:vertices.append(p);uvs.append([p[axes[0]]/2,p[axes[1]]/2])
    for i in range(1,len(points)-1):triangles.append([start,start+i+1,start+i])
def inner(u):
    w=2.4 if u<8 else 2.4+(u-8)/10*1.2
    return [[u,-w,0],[u,w,0],[u,w,2.3],[u,w*.72,3.35],[u,0,3.9],[u,-w*.72,3.35],[u,-w,2.3]]
def outer(u):
    # Variable silhouette with toes meeting the untouched ridge. The back and
    # entrance are physically closed annuli, with only the gallery left open.
    wobble=.35*math.sin(u*.71)+.2*math.cos(u*1.7);w=8.7+wobble
    return [[u,-w,-2.1],[u,w,-2.1],[u,w*.9,2.7+wobble],[u,w*.56,6.3+wobble],[u,.35*math.sin(u),7.2+wobble],[u,-w*.6,6.1-wobble],[u,-w*.94,2.8-wobble]]
sections=list(range(-8,19,2))
for u0,u1 in zip(sections,sections[1:]):
    i0,i1,o0,o1=inner(u0),inner(u1),outer(u0),outer(u1)
    for j in range(7):
        k=(j+1)%7
        # Inner faces point into the cavity; outer faces out into the world.
        face([i0[k],i0[j],i1[j],i1[k]])
        face([o0[j],o0[k],o1[k],o1[j]])
for u,reverse in [(-8,False),(18,True)]:
    a,b=inner(u),outer(u)
    for j in range(7):
        k=(j+1)%7;q=[a[k],a[j],b[j],b[k]];face(q if reverse else list(reversed(q)))
# End face blocks the gallery, without adding a solid volume across the entry.
face(list(reversed(inner(18))))
clearance=[]
for u in range(-8,19):
    for v in [-2,-1,0,1,2]:
        h=T.height(*xy(u,v));assert floor-h>=.179
        clearance.append(floor-h)
entry=xy(-8,0);outside=xy(-13,0);outside_ground=T.height(*outside)
assert 0<floor-outside_ground<3
plan=dict(id='H08.RidgeMine',target_id='H08',stage='STRUCTURE_DRAFT',owner='ProjectJ.Starter.H08.Mine.v1',center_m=[cx,cy,floor],yaw_degrees=yaw,mesh_asset='/Game/World/Starter/Geometry/SM_H08_RidgeMine',height_sha256=T.manifest['terrain_profile']['sha256'],local_axis_m=[-8,18],gallery_length_m=26,clear_width_m=4.8,chamber_width_m=7.2,clear_height_m=3.9,terrain_floor_clearance_m=[min(clearance),max(clearance)],terrain_modified=False,vertices_m=vertices,triangles=triangles,uv0=uvs,entrance_m=[*entry,floor],outside_m=[*outside,outside_ground],timber_frame_stations_m=[-7.5,-3,2,7,12,16],art_complete=False,gameplay_bound=False,scope='Physical interior and entry draft only; whole H08 layout, approaches and final art pending')
FILE.write_text(json.dumps(plan,indent=2),encoding='utf-8');print('Mine floor',floor,'terrain clearance',plan['terrain_floor_clearance_m'],'entrance',plan['entrance_m'])
