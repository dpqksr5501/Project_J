"""Isolated production validation/save contracts; never loads an actual UE world."""
import ast
import copy
import json
import math
from pathlib import Path
import runpy
from types import SimpleNamespace
import unittest
import importlib.util
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]

class Vec:
    def __init__(self, *v): self.v = tuple(v)
    def __sub__(self, other): return Vec(*(a-b for a,b in zip(self.v, other.v)))
    def length(self): return math.sqrt(sum(x*x for x in self.v))
    def to_tuple(self): return self.v

class Prop:
    def __init__(self, **properties): self.properties = properties
    def get_editor_property(self, name): return self.properties[name]

class WorldPlanTests(unittest.TestCase):
    def setUp(self): self.plan=json.loads((ROOT/'Content/World/Starter/Source/StarterWorld.plan.json').read_text(encoding='utf-8'))
    def native(self, plan):
        return [Prop(place_id=p['id'], display_name=p['name'], location=Vec((p['center_m'][0]-4032)*100,(p['center_m'][1]-4032)*100,p['center_m'][2]*100),
            radius=p['radius_m']*100,environment_id=p['kind'],place_type=p['kind'],recommended_min_level=p['levels'][0],recommended_max_level=p['levels'][1],
            boundary=[Vec((q[0]-4032)*100,(q[1]-4032)*100,q[2]*100 if len(q)>2 else 0) for q in p['boundary_m']]) for p in plan['places']]
    def validate_actual_prefix(self, plan, places):
        # Execute the actual verifier's pre-terrain validation prefix, not a copy
        # of its assertions; later landscape/PIE checks need a separate UE run.
        parsed=ast.parse((ROOT/'Scripts/World/Verify-StarterWorld.py').read_text(encoding='utf-8'))
        fn=next(n for n in parsed.body if isinstance(n,ast.FunctionDef) and n.name=='verify')
        body=next(n for n in fn.body if isinstance(n,ast.Try)).body
        prefix=[]
        for node in body:
            if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='connections' for t in node.targets): break
            prefix.append(node)
        state_type=type('State',(Prop,),{})
        state=state_type(definition=Prop(places=places))
        fake=SimpleNamespace(EditorActorSubsystem='Actors',Project_JWorldState=state_type,Vector=Vec,
            get_editor_subsystem=lambda _:SimpleNamespace(get_all_level_actors=lambda:[state]))
        manifest=json.loads((ROOT/'Content/World/Starter/Source/StarterRegion.manifest.json').read_text(encoding='utf-8'))
        scope=dict(unreal=fake,plan=plan,manifest=manifest,CONTRACT=runpy.run_path(str(ROOT/'Scripts/World/Starter-WorldDefinitionContract.py')))
        exec(compile(ast.Module(body=prefix,type_ignores=[]),'actual-verifier-prefix','exec'),scope)
    def test_current_twenty_places_and_four_point_boundary(self):
        self.validate_actual_prefix(self.plan,self.native(self.plan))
    def test_explicit_historical_eighteen_place_input(self):
        plan=copy.deepcopy(self.plan);plan['places']=plan['places'][:18]
        self.validate_actual_prefix(plan,self.native(plan))
    def test_missing_or_extra_place_is_rejected(self):
        with self.assertRaises(AssertionError): self.validate_actual_prefix(self.plan,self.native(self.plan)[:-1])
    def test_equal_vertex_count_with_wrong_shape_is_rejected(self):
        plan=copy.deepcopy(self.plan);plan['places']=plan['places'][:18]
        places=self.native(plan);places[0].properties['boundary'][0].v=(0,0,0)
        with self.assertRaises(AssertionError): self.validate_actual_prefix(plan,places)

class OwnedSaveTests(unittest.TestCase):
    def setUp(self):
        self.dirty=[];self.saved=[];self.save_ok=True
        self.manifest=dict(owned_content_root='/Game/World/Starter',world_asset='/Game/World/Starter/Maps/L_StarterRegion',
            external_actor_root='/Game/__ExternalActors__/World/Starter/',external_object_root='/Game/__ExternalObjects__/World/Starter/',
            world_grid=dict(unreal_origin_cm=[0,0,0],centimetres_per_source_metre=100))
        self.actor_type=type('Actor',(),{})
        self.fake=SimpleNamespace(Actor=self.actor_type,
            EditorLoadingAndSavingUtils=SimpleNamespace(get_dirty_map_packages=lambda:list(self.dirty),get_dirty_content_packages=lambda:[],save_packages=self.save),
            EditorLevelLibrary=SimpleNamespace(get_pie_worlds=lambda _:[],get_editor_world=lambda:SimpleNamespace(get_path_name=lambda:self.manifest['world_asset']+'.World')),
            EditorActorSubsystem='Actors',get_editor_subsystem=lambda _:SimpleNamespace(get_all_level_actors=lambda:[]),
            Project_JWorldAuthoringLibrary=SimpleNamespace(get_authoring_package=lambda obj:obj.package))
        self.patch=patch.dict(sys.modules,unreal=self.fake);self.patch.start();self.addCleanup(self.patch.stop)
        spec=importlib.util.spec_from_file_location('isolated_settlement',ROOT/'Scripts/World/Starter-SettlementAuthoring.py')
        self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
    def package(self,suffix): return SimpleNamespace(get_name=lambda:'/Game/__ExternalActors__/World/Starter/'+suffix)
    def actor(self,package):
        value=self.actor_type();value.package=package;value.get_actor_label=lambda:'Owned';value.get_outermost=lambda:package;return value
    def save(self,packages,only_dirty):
        self.saved.append([p.get_name() for p in packages]);return self.save_ok
    def author(self): return self.module.SettlementAuthoring('Audit','Audit',self.manifest)
    def test_existing_unrelated_dirty_is_refused_before_authoring(self):
        self.dirty=[self.package('Unrelated')]
        with self.assertRaises((AssertionError,RuntimeError)): self.author()
        self.assertFalse(self.saved)
    def test_concurrent_unrelated_same_world_edit_is_never_saved(self):
        author=self.author();own=self.package('Owned');other=self.package('Other');author._owned_objects=[self.actor(own)]
        self.dirty=[own,other]
        with self.assertRaises((AssertionError,RuntimeError)):author.save()
        self.assertFalse(self.saved)
    def test_exact_owned_package_saves(self):
        author=self.author();own=self.package('Owned');author._owned_objects=[self.actor(own)];self.dirty=[own]
        author.save();self.assertEqual(self.saved,[[own.get_name()]])
    def test_failed_save_propagates_without_unrelated_fallback(self):
        author=self.author();own=self.package('Owned');author._owned_objects=[self.actor(own)];self.dirty=[own];self.save_ok=False
        with self.assertRaises((AssertionError,RuntimeError)):author.save()
        self.assertEqual(self.saved,[[own.get_name()]])

if __name__=='__main__': unittest.main()
