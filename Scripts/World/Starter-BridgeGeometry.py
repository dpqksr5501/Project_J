"""Owned stone viaduct for S01's measured river crossing.

Uses the existing provider's PBR stone textures with metre-scaled UVs. Geometry
is an editable saved StaticMesh, not runtime construction or scaled road planes.
"""
import hashlib,json,math
from pathlib import Path
import unreal
BASE='/Game/World/Starter';OWNER='ProjectJ.Starter.Corridor.v1'


def build(height,bx,by,heading):
    library=unreal.EditorAssetLibrary;edit=unreal.MaterialEditingLibrary
    tools=unreal.AssetToolsHelpers.get_asset_tools()
    library.make_directory(BASE+'/Geometry')
    mat_path=BASE+'/Materials/M_StarterBridgeStone'
    material=unreal.load_asset(mat_path)
    if material is None:
        material=tools.create_asset('M_StarterBridgeStone',BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew());assert material
        library.set_metadata_tag(material,'ProjectJ.Authoring',OWNER)
    # A failed first compilation may leave this new, unsaved material in memory.
    # Never adopt an existing untagged file from somebody else's authoring.
    if not library.get_metadata_tag(material,'ProjectJ.Authoring'):
        assert not (Path(unreal.Paths.project_content_dir())/'World/Starter/Materials/M_StarterBridgeStone.uasset').exists()
        library.set_metadata_tag(material,'ProjectJ.Authoring',OWNER)
    assert library.get_metadata_tag(material,'ProjectJ.Authoring')==OWNER
    material.modify()
    for node in list(edit.get_material_expressions(material)):edit.delete_material_expression(material,node)
    for i,(suffix,property,output) in enumerate([('Color',unreal.MaterialProperty.MP_BASE_COLOR,'RGB'),('NormalDX',unreal.MaterialProperty.MP_NORMAL,'RGB'),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS,'R')]):
        texture=unreal.load_asset('/Game/Map_Resource/MedCastle/Textures/T_PavingStones142_2K-PNG_'+suffix);assert texture
        node=edit.create_material_expression(material,unreal.MaterialExpressionTextureSample,-350,i*180)
        node.set_editor_property('texture',texture)
        if suffix=='NormalDX':node.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_NORMAL if texture.get_editor_property('virtual_texture_streaming') else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        assert edit.connect_material_property(node,output,property)
    errors=edit.recompile_material(material);assert not errors,list(errors)
    library.save_loaded_asset(material)
    a=math.radians(heading);dx,dy=math.cos(a),math.sin(a)
    length=100.;base=72.;width=8.;left=height(bx-dx*length/2,by-dy*length/2)+.08;right=height(bx+dx*length/2,by+dy*length/2)+.08
    def deck(x):
        t=(x+length/2)/length
        return left*(1-t)+right*t+3.4*math.sin(math.pi*t)
    def floor(x):
        # Pier toes meet the real riverbed/bank at both sides of the structure.
        return min(height(bx+dx*x-dy*y,by+dy*x+dx*y) for y in [-width/2,width/2])-.55
    vertices=[];triangles=[];normals=[];uvs=[]
    def v(x,y,z):return (x*100,y*100,(z-base)*100)
    def quad(a,b,c,d):
        ab=unreal.Vector(*b)-unreal.Vector(*a);ac=unreal.Vector(*c)-unreal.Vector(*a);n=ab.cross(ac).normal()
        start=len(vertices)
        for p in [a,b,c,d]:
            vertices.append(unreal.Vector(*p));normals.append(n)
            if abs(n.z)>.7:uv=(p[0]/200,p[1]/200)
            elif abs(n.y)>.7:uv=(p[0]/200,p[2]/200)
            else:uv=(p[1]/200,p[2]/200)
            uvs.append(unreal.Vector2D(*uv))
        # StaticMesh front faces use clockwise winding. Physics must hit the
        # roadway's upper face, not pass through it and hit its underside.
        triangles.extend([unreal.IntVector(start,start+2,start+1),unreal.IntVector(start,start+3,start+2)])
    def strip(x0,x1,y0,y1,lo0,lo1,hi0,hi1):
        # Closed wedge between two cross sections. Isolated face vertices keep
        # the deck/parapet edges crisp and the stone normal map correctly framed.
        a,b,c,d=v(x0,y0,lo0),v(x1,y0,lo1),v(x1,y1,lo1),v(x0,y1,lo0)
        e,f,g,h=v(x0,y0,hi0),v(x1,y0,hi1),v(x1,y1,hi1),v(x0,y1,hi0)
        quad(e,f,g,h);quad(d,c,b,a);quad(a,b,f,e);quad(c,d,h,g);quad(d,a,e,h);quad(b,c,g,f)
    # Continuous cambered roadway with a real thickness and end heights matching
    # the approach terrain. Separate parapet/coping geometry gives a readable edge.
    for i in range(100):
        x0=-50+i;x1=x0+1;z0,z1=deck(x0),deck(x1)
        strip(x0,x1,-4.25,4.25,z0-.78,z1-.78,z0,z1)
        for sign in [-1,1]:
            yc=sign*4.25
            strip(x0,x1,yc-.28,yc+.28,z0,z1,z0+.92,z1+.92)
            strip(x0,x1,yc-.38,yc+.38,z0+.92,z1+.92,z0+1.06,z1+1.06)
    # Six elliptical arch openings. Fill above the curved intrados; the space
    # below remains open all the way across the channel for water and sightlines.
    openings=[(-41+14*i,-29+14*i) for i in range(6)]
    for start,end in openings:
        spring=74.8;peak=deck((start+end)/2)-1.55
        def intrados(x):
            t=(x-start)/(end-start)*2-1
            return spring+(peak-spring)*math.sqrt(max(0,1-t*t))
        for i in range(32):
            x0=start+(end-start)*i/32;x1=start+(end-start)*(i+1)/32
            strip(x0,x1,-4.12,4.12,intrados(x0),intrados(x1),deck(x0)-.78,deck(x1)-.78)
    # Integrated piers and abutments are part of this single asset, not hundreds
    # of always-loaded primitive actors. Footing elevations follow the source bed.
    solids=[(-50,-41),(41,50)]+[(-29+14*i,-27+14*i) for i in range(5)]
    for start,end in solids:
        sections=max(2,math.ceil((end-start)/2))
        for i in range(sections):
            x0=start+(end-start)*i/sections;x1=start+(end-start)*(i+1)/sections
            strip(x0,x1,-4.12,4.12,floor(x0),floor(x1),deck(x0)-.78,deck(x1)-.78)
    for x in range(-48,49,8):
        for sign in [-1,1]:strip(x-.48,x+.48,sign*4.25-.48,sign*4.25+.48,deck(x)+.92,deck(x)+.92,deck(x)+1.2,deck(x)+1.2)
    fingerprint=hashlib.sha256(json.dumps(dict(vertices=[list(p.to_tuple()) for p in vertices],triangles=[list(t.to_tuple()) for t in triangles],material=mat_path),separators=(',',':')).encode()).hexdigest()
    path=BASE+'/Geometry/SM_S01_StoneBridge';mesh=unreal.load_asset(path)
    if mesh:
        assert library.get_metadata_tag(mesh,'ProjectJ.Authoring')==OWNER
        if library.get_metadata_tag(mesh,'ProjectJ.SourceHash')==fingerprint:return mesh,dict(center_m=[bx,by],heading=heading,span_m=length,bank_heights_m=[left,right],base_m=base,triangles=len(triangles),arches=6)
    dynamic=unreal.DynamicMesh();buffers=unreal.GeometryScriptSimpleMeshBuffers()
    buffers.set_editor_property('vertices',vertices);buffers.set_editor_property('triangles',triangles);buffers.set_editor_property('normals',normals);buffers.set_editor_property('uv0',uvs)
    unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers)
    if mesh:
        mesh.modify();options=unreal.GeometryScriptCopyMeshToAssetOptions();options.set_editor_property('enable_recompute_normals',True);options.set_editor_property('enable_recompute_tangents',True)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dynamic,mesh,options,unreal.GeometryScriptMeshWriteLOD())
    else:
        options=unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property('enable_collision',True);options.set_editor_property('collision_mode',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        options.set_editor_property('enable_recompute_normals',True);options.set_editor_property('enable_recompute_tangents',True)
        result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic,path,options);mesh=result[0];assert mesh
    mesh.set_material(0,material)
    library.set_metadata_tag(mesh,'ProjectJ.Authoring',OWNER);library.set_metadata_tag(mesh,'ProjectJ.SourceHash',fingerprint)
    library.save_loaded_asset(mesh)
    return mesh,dict(center_m=[bx,by],heading=heading,span_m=length,bank_heights_m=[left,right],base_m=base,triangles=len(triangles),arches=6)
