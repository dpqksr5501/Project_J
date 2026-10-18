"""Explicit package ownership for one authoring operation; no path-wide saves.

Reject pre-existing dirty state and stop on another package becoming dirty between
chunks. A shared non-external map package is only supported for a newly-created
map; existing non-WP shared-map edits need their own exclusive authoring workflow.
"""
import unreal

def dirty_packages():
    return {p.get_name():p for p in
        list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+
        list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())}

class OwnedSaveScope:
    def __init__(self, new_world_package=None):
        assert not dirty_packages(), 'Refuse pre-existing dirty packages before authoring'
        self.owned={}
        self.new_world_package=new_world_package
    def own(self, obj):
        assert obj is not None
        package=obj.get_outermost()
        assert package, 'Object has no saveable authoring package'
        name=package.get_name()
        if isinstance(obj,unreal.Actor):
            assert name.startswith('/Game/__ExternalActors__/') or name==self.new_world_package, 'Cannot claim an existing shared map through one actor'
        self.owned[name]=package
        return obj
    def save(self):
        dirty=dirty_packages()
        foreign=sorted(set(dirty)-set(self.owned))
        assert not foreign, 'Unrelated concurrent edits; nothing saved: '+str(foreign)
        selected=[dirty[name] for name in sorted(dirty) if name in self.owned]
        if selected:
            assert unreal.EditorLoadingAndSavingUtils.save_packages(selected,True), 'Owned package save failed; no broad-save fallback'
        return [p.get_name() for p in selected]
