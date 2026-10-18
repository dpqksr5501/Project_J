"""Stable authoring-place snapshot; no mutable weather/server state ownership."""
import math

def validate_places(plan, places, origin=(-403200, -403200, 0), units=100):
    """Compare supplied plan identities and polygons, not a historical stage count."""
    expected={p['id']:p for p in plan['places']}
    actual={p['id']:p for p in places}
    assert expected and len(expected)==len(plan['places']), 'Duplicate/empty source place IDs'
    assert len(actual)==len(places) and set(actual)==set(expected), 'Saved/source place IDs differ'
    for key, entry in expected.items():
        value=actual[key]
        assert [value['min_level'],value['max_level']]==entry['levels'],key
        center=[origin[i]+entry['center_m'][i]*units for i in range(3)]
        assert math.dist(value['location_cm'],center)<1,key
        source=entry['boundary_m']; boundary=value['boundary_cm']
        assert len(source)>=3 and len(boundary)==len(source),key
        assert all(len(p)>=2 and all(math.isfinite(x) for x in p) for p in source),key
        area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(source,source[1:]+source[:1]))
        assert abs(area)>1e-6 and len({tuple(p[:2]) for p in source})==len(source),key
        for source_point, saved_point in zip(source,boundary):
            assert len(saved_point)==3 and all(math.isfinite(x) for x in saved_point),key
            assert math.dist(saved_point[:2],[origin[i]+source_point[i]*units for i in range(2)])<1,key
            if len(source_point)==3: assert abs(saved_point[2]-(origin[2]+source_point[2]*units))<1,key
    return len(actual)

def snapshot_place(place):
    return dict(id=str(place.get_editor_property('place_id')),display_name=str(place.get_editor_property('display_name')),location_cm=list(place.get_editor_property('location').to_tuple()),radius=place.get_editor_property('radius'),environment_id=str(place.get_editor_property('environment_id')),place_type=str(place.get_editor_property('place_type')),min_level=place.get_editor_property('recommended_min_level'),max_level=place.get_editor_property('recommended_max_level'),boundary_cm=[list(q.to_tuple()) for q in place.get_editor_property('boundary')])

def snapshot_connection(connection):
    return dict(id=str(connection.get_editor_property('connection_id')),side=str(connection.get_editor_property('boundary_side')),kind=str(connection.get_editor_property('connection_type')),location_cm=list(connection.get_editor_property('location').to_tuple()),outward=list(connection.get_editor_property('outward_direction').to_tuple()),width_cm=connection.get_editor_property('width_cm'))
