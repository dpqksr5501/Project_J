"""Shared native static placement for authored settlements; no startup hooks."""
import hashlib,importlib.util,json,math,runpy
from pathlib import Path
import unreal
class SettlementAuthoring:
    def instanced_material(self,interface):
        if interface.get_base_material().get_editor_property('used_with_instanced_static_meshes'):return interface
        lib=unreal.EditorAssetLibrary;edit=unreal.MaterialEditingLibrary
        path=self.resource_base+'/Materials/SettlementInstancing/'+interface.get_name()+'_'+hashlib.sha256(interface.get_path_name().encode()).hexdigest()[:8]
        copy=unreal.load_asset(path)
        if copy is None:
            lib.make_directory(self.resource_base+'/Materials/SettlementInstancing')
            copy=lib.duplicate_asset(interface.get_path_name().split('.')[0],path);assert copy
            self._owned_objects.append(copy)
            lib.set_metadata_tag(copy,'ProjectJ.Authoring','ProjectJ.Starter.SettlementInstancing.v1')
            if isinstance(copy,unreal.Material):edit.set_material_usage(copy,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            else:edit.set_material_instance_parent(copy,self.instanced_material(interface.get_editor_property('parent')))
        assert lib.get_metadata_tag(copy,'ProjectJ.Authoring')=='ProjectJ.Starter.SettlementInstancing.v1'
        return copy
    def __init__(self,tag,folder,manifest=None):
        self._save_scope=runpy.run_path(str(Path(__file__).with_name('Starter-OwnedSave.py')))['OwnedSaveScope']()
        self._owned_objects=[]
        if manifest is None:
            manifest=json.loads((Path(__file__).resolve().parents[2]/'Content/World/MainWorld/Source/MainWorld.manifest.json').read_text(encoding='utf-8'))
        self.manifest=manifest;self.base=manifest['owned_content_root'];self.resource_base=manifest['resource_content_root'];self.world_asset=manifest['world_asset']
        self.origin=manifest['world_grid']['unreal_origin_cm'];self.units=manifest['world_grid']['centimetres_per_source_metre']
        assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
        assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith(self.world_asset+'.')
        self.tag,self.folder=tag,folder
        self.subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.labels={a.get_actor_label() for a in self.subsystem.get_all_level_actors()}
        self.meshes={};self.created=[]
    def point(self,x,y,z):
        return unreal.Vector(self.origin[0]+x*self.units,self.origin[1]+y*self.units,self.origin[2]+z*self.units)
    def supported_house(self,label,house,height,construction,stone):
        """Place a scanned shell on a surveyed foundation without editing terrain."""
        assert self.units==100,'Scanned construction kit is authored in centimetres'
        x,y=house['x'],house['y'];w,d=house['width'],house['depth'];yaw=house['yaw']
        a=math.radians(yaw);co,si=math.cos(a),math.sin(a)
        samples=[height(x+co*u-si*v,y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]]
        assert max(samples)-min(samples)<2.5,(label,'Unsupported plot; preserve terrain')
        floor=max(samples)+.06;bottom=min(samples)-.25;cube=construction.mesh('/Engine/BasicShapes/Cube')
        actor=self.spawn(label,'/Engine/BasicShapes/Cube',x,y,floor,yaw)
        actor.static_mesh_component.set_static_mesh(None)
        actor.set_actor_transform(unreal.Transform(location=self.point(x,y,floor),rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0),scale=unreal.Vector(1,1,1)),False,False)
        self.created[-1].update(mesh=None,location_cm=list(actor.get_actor_location().to_tuple()))
        construction.assemble_modular_house(actor,dict(house,ground_m=floor))
        foundation=construction.component(actor,unreal.StaticMeshComponent,'Foundation');foundation.set_static_mesh(cube);foundation.set_material(0,stone);foundation.set_collision_profile_name('BlockAll')
        depth=floor-bottom;foundation.set_relative_transform(construction.centered_transform(cube,unreal.Vector(0,0,-depth*100),0,(w+.24,d+.24,depth)),False,False)
        return actor,dict(id=house['id'],label=label,center_m=[x,y,floor],yaw=yaw,width=w,depth=d,storeys=house.get('storeys',1),foundation_bottom_m=bottom)
    def add_access_stairs(self,actor,placement,height,construction,stone):
        """Fit a short exterior stair to the existing sill and preserved terrain."""
        self._owned_objects.append(actor)
        assert not any(c.get_name()=='EntryStairs' for c in actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent))
        spec=importlib.util.spec_from_file_location('settlement_support',Path(__file__).with_name('Starter-SettlementSupport.py'));support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
        planned=support.plan_access_stairs(placement,height);pieces=planned.pop('pieces');floor=placement['center_m'][2];cube=construction.mesh('/Engine/BasicShapes/Cube')
        c=construction.component(actor,unreal.HierarchicalInstancedStaticMeshComponent,'EntryStairs');c.set_static_mesh(cube);c.set_material(0,self.instanced_material(stone));c.set_collision_profile_name('BlockAll')
        for distance,depth,top,bottom in pieces:
            c.add_instance(construction.centered_transform(cube,unreal.Vector(0,-distance*100,(bottom-floor)*100),0,(1.8,depth,top-bottom)),False)
        return planned
    def spawn(self,label,path,x,y,z,yaw=0,scale=(1,1,1),material=None,collision=True):
        assert label not in self.labels,label
        if path not in self.meshes:self.meshes[path]=unreal.load_asset(path)
        mesh=self.meshes[path];assert isinstance(mesh,unreal.StaticMesh),path
        b=mesh.get_bounds();o,e=b.origin,b.box_extent;a=math.radians(yaw);c,s=math.cos(a),math.sin(a)
        p=self.point(x,y,z)-unreal.Vector(c*o.x*scale[0]-s*o.y*scale[1],s*o.x*scale[0]+c*o.y*scale[1],(o.z-e.z)*scale[2])
        actor=self.subsystem.spawn_actor_from_class(unreal.StaticMeshActor,p);assert actor
        self._owned_objects.append(actor)
        actor.modify();actor.set_actor_label(label);actor.set_folder_path(self.folder)
        actor.set_editor_property('tags',[unreal.Name(self.tag)])
        actor.set_editor_property('is_spatially_loaded',True)
        comp=actor.static_mesh_component;comp.modify();comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(mesh);comp.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        if material:comp.set_material(0,material)
        actor.set_actor_transform(unreal.Transform(location=p,rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0),scale=unreal.Vector(*scale)),False,False)
        self.labels.add(label);self.created.append(dict(label=label,path=actor.get_path_name(),mesh=path,location_cm=list(p.to_tuple())))
        return actor
    @staticmethod
    def add_static_component(owner,path,name,component_class=None):
        subsystem=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib=unreal.SubobjectDataBlueprintFunctionLibrary
        root=next(h for h in subsystem.k2_gather_subobject_data_for_instance(owner) if lib.is_root_component(lib.get_data(h)))
        component_class=component_class or unreal.StaticMeshComponent
        handle,reason=subsystem.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root,new_class=component_class,blueprint_context=None))
        assert reason.is_empty(),str(reason)
        subsystem.rename_subobject(handle=handle,new_name=unreal.Text(name))
        component=lib.get_associated_object(lib.get_data(handle));component.modify()
        component.set_mobility(unreal.ComponentMobility.STATIC);component.set_static_mesh(unreal.load_asset(path))
        component.set_collision_profile_name('NoCollision')
        component.set_relative_transform(unreal.Transform(),False,False)
        return component
    def save(self):
        for obj in self._owned_objects:self._save_scope.own(obj)
        return self._save_scope.save()
