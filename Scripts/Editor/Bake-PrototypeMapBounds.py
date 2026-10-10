"""Read one prototype level and bake approximate static bounds into the new map definition.
Does not save or modify the level. Runtime never scans actors for map geometry."""
import unreal

assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/ThirdPerson/Lvl_ThirdPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
regions = []
minimum = [float('inf'), float('inf')]
maximum = [float('-inf'), float('-inf')]
for actor in actors:
    if not isinstance(actor, unreal.StaticMeshActor):
        continue
    origin, extent = actor.get_actor_bounds(False)
    if extent.x <= 0 or extent.y <= 0 or extent.x > 20000 or extent.y > 20000:
        continue
    lo = unreal.Vector2D(origin.x - extent.x, origin.y - extent.y)
    hi = unreal.Vector2D(origin.x + extent.x, origin.y + extent.y)
    minimum[0] = min(minimum[0], lo.x)
    minimum[1] = min(minimum[1], lo.y)
    maximum[0] = max(maximum[0], hi.x)
    maximum[1] = max(maximum[1], hi.y)
    if 'floor' in actor.get_actor_label().lower():
        continue
    box = unreal.Box2D()
    box.set_editor_property('min', lo)
    box.set_editor_property('max', hi)
    box.set_editor_property('is_valid', 1)
    regions.append(box)
assert regions and len(regions) <= 256
center = unreal.Vector2D((minimum[0]+maximum[0])/2, (minimum[1]+maximum[1])/2)
half = max((maximum[0]-minimum[0])/2, (maximum[1]-minimum[1])/2) + 200
asset = unreal.load_asset('/Game/UI/DA_ProjectJMap')
assert asset
asset.set_editor_property('regions', regions)
asset.set_editor_property('center', center)
asset.set_editor_property('half_extent', unreal.Vector2D(half,half))
asset.set_editor_property('level_name', 'Lvl_ThirdPerson')
assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
print('PROJECT_J_MAP_BOUNDS_BAKED regions=%d center=%s half=%s' % (len(regions),center,half))
