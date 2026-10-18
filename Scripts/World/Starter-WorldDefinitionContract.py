"""Stable authoring-place snapshot; no mutable weather/server state ownership."""
def snapshot_place(place):
    return dict(id=str(place.get_editor_property('place_id')),display_name=str(place.get_editor_property('display_name')),location_cm=list(place.get_editor_property('location').to_tuple()),radius=place.get_editor_property('radius'),environment_id=str(place.get_editor_property('environment_id')),place_type=str(place.get_editor_property('place_type')),min_level=place.get_editor_property('recommended_min_level'),max_level=place.get_editor_property('recommended_max_level'),boundary_cm=[list(q.to_tuple()) for q in place.get_editor_property('boundary')])

def snapshot_connection(connection):
    return dict(id=str(connection.get_editor_property('connection_id')),side=str(connection.get_editor_property('boundary_side')),kind=str(connection.get_editor_property('connection_type')),location_cm=list(connection.get_editor_property('location').to_tuple()),outward=list(connection.get_editor_property('outward_direction').to_tuple()),width_cm=connection.get_editor_property('width_cm'))
