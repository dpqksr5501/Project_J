"""Check a bounded ProjectJ.CaptureMoverBuffers CSV and its completion receipt."""
import argparse
import csv
import json
import math
import re
from collections import defaultdict, Counter
from bisect import bisect_right
from pathlib import Path


def validate_diagnostics(path):
    lines=Path(path).read_text(encoding='utf-8-sig',errors='replace').splitlines()
    start=next((i for i,line in enumerate(lines) if 'MoverBufferCapture Started ' in line),None)
    assert start is not None, 'No explicit post-readiness capture boundary in log'
    pattern=re.compile(r'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault')
    startup=[line for i,line in enumerate(lines) if i<start and pattern.search(line)]
    measured=[line for i,line in enumerate(lines) if i>=start and pattern.search(line)]
    assert not measured, 'NetworkPrediction diagnostic during the captured stream: '+str(measured[:3])
    return dict(startup_diagnostics=startup,captured_diagnostics=measured,capture_start_line=start+1)


def validate_raw_receives(path):
    """Bounded actor-value evidence, not a packet capture or interpolation pass override."""
    path=Path(path)
    receipt=json.loads(path.with_suffix('.summary.json').read_text(encoding='utf-8-sig'))
    assert receipt.get('raw_receive_requested') is True, 'Raw receive observation was not requested'
    assert receipt.get('raw_receive_capable') is True and receipt.get('raw_receive_enabled') is True, 'Raw receive capability missing or disabled'
    assert receipt.get('raw_receive_thread_supported') is True, 'Unsupported receive writer thread'
    size=receipt.get('raw_receive_snapshot_bytes')
    assert type(size) is int and 0<size<=64, 'Invalid fixed receive snapshot size'
    assert receipt.get('raw_receive_max_tracked_snapshot_bytes')==size*4*1024, 'Invalid tracked receive snapshot bound'
    started=receipt.get('capture_started_seconds')
    assert type(started) in (int,float) and math.isfinite(started) and started>=0, 'Invalid receive capture clock'
    streams={}; client_rows=received_rows=0
    required={'World','Actor','Role','WallSeconds','RawStatus','RawSimID','RawBindingEpoch','RawChannel',
              'RawApplySerial','RawDecodeSeconds','RawDecodeEngineFrame','RawFrameKind','RawFrameValue',
              'RawTimeKind','RawTimeMS','RawViewTimeMS','RawWorldInterpolationMS'}
    with path.with_suffix('.actors.csv').open(encoding='utf-8-sig',newline='') as file:
        reader=csv.DictReader(file)
        assert required.issubset(reader.fieldnames or []), 'Missing raw receive actor columns'
        for row in reader:
            status=row['RawStatus'];channel=row['RawChannel']
            if status=='NotClient':
                assert channel=='None', 'Authority row has a receive channel'
                continue
            client_rows+=1
            assert status in ('AwaitingReceive','BeforeCapture','Received'), 'Incomplete receive source: '+status
            assert channel in ('AP','SP') and int(row['Role'])==(2 if channel=='AP' else 1), 'Receive role/channel mismatch'
            sim=int(row['RawSimID']);epoch=int(row['RawBindingEpoch']);serial=int(row['RawApplySerial'])
            assert sim>0 and 0<epoch<=2**64-1 and 0<=serial<=2**64-1, 'Invalid receive binding identity/counter'
            key=(row['World'],row['Actor'],sim,epoch,channel)
            record=streams.setdefault(key,dict(samples=0,received_rows=0,coalesced_applies=0,wire_time_repeats=0,
                                             wire_time_regressions=0,max_view_behind_world_ms=None,last=None))
            record['samples']+=1
            if status!='Received':
                assert status!='AwaitingReceive' or serial==0, 'Awaiting source contains decoded applies'
                continue
            received_rows+=1;record['received_rows']+=1
            decode=float(row['RawDecodeSeconds']);wall=float(row['WallSeconds']);engine=int(row['RawDecodeEngineFrame'])
            fk=int(row['RawFrameKind']);frame=int(row['RawFrameValue']);tk=int(row['RawTimeKind']);time=int(row['RawTimeMS'])
            view=float(row['RawViewTimeMS']);interp=float(row['RawWorldInterpolationMS'])
            assert all(math.isfinite(v) for v in (decode,wall,view,interp)) and serial>0 and engine>=0, 'Invalid decoded receive values'
            assert started<=decode<=started+wall+.001, 'Receive value is outside this capture/row clock'
            if channel=='SP':
                assert (fk,tk) in ((0,1),(1,0)), 'SP received wrong frame/time domain'
            else:
                assert (fk,tk) in ((2,2),(1,0)), 'AP received wrong frame/time domain'
            assert fk!=0 or frame==-1, 'Absent wire frame contains a guessed local/server frame'
            assert tk!=0 or time==-1, 'Absent wire time contains a derived time'
            previous=record['last']
            current=dict(serial=serial,decode_seconds=decode,engine_frame=engine,frame_kind=fk,frame=frame,time_kind=tk,time_ms=time)
            if previous:
                assert serial>=previous['serial'], 'Decode serial rewound within a binding'
                assert decode>=previous['decode_seconds'], 'Local decode clock rewound'
                if serial==previous['serial']:
                    assert current==previous, 'Receive snapshot changed without a decode apply'
                else:
                    record['coalesced_applies']+=max(0,serial-previous['serial']-1)
                    if tk and tk==previous['time_kind']:
                        record['wire_time_repeats']+=time==previous['time_ms']
                        record['wire_time_regressions']+=time<previous['time_ms']
            # Only independent SP server-variable clock is comparable to shared interpolation.
            if channel=='SP' and tk==1:
                behind=interp-view
                old=record['max_view_behind_world_ms']
                record['max_view_behind_world_ms']=behind if old is None else max(old,behind)
            record['last']=current
    assert receipt.get('raw_receive_client_rows')==client_rows, 'Raw client row count differs from completion metadata'
    assert receipt.get('raw_receive_received_rows')==received_rows and received_rows>0, 'No current-session receive rows or count mismatch'
    result=[]
    for key,record in streams.items():
        result.append(dict(world=key[0],actor=key[1],sim_id=key[2],binding_epoch=key[3],channel=key[4],**record))
    return dict(semantics='NP model decode/apply; not packets. Coalesced intervals cannot reconstruct every receive.',
                snapshot_bytes=size,client_rows=client_rows,received_rows=received_rows,streams=result)


def read_completion(path):
    path=Path(path)
    receipt_path=path.with_suffix('.summary.json')
    assert receipt_path.is_file(), 'Missing capture completion metadata: '+str(receipt_path)
    receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    assert receipt.get('schema_version')==1, 'Unsupported capture completion metadata'
    assert receipt.get('complete') is True and receipt.get('truncated') is False, 'Partial capture: '+str(receipt.get('stop_reason'))
    assert receipt.get('stop_reason') in ('Duration','EnginePreExit','ExplicitStop'), 'Unknown capture stop reason'
    assert receipt.get('csv_saved') is True and receipt.get('actors_csv_saved') is True, 'Capture CSV save failed'
    peak=receipt.get('peak_retained_bytes'); limit=receipt.get('max_retained_bytes')
    assert type(peak) is int and type(limit) is int and 0<=peak<=limit<=18*1024*1024, 'Capture exceeded retained memory budget'
    for peak_key,limit_key,maximum in (('peak_components','max_components',1024),
                                       ('peak_discovery_actor_visits','max_discovery_actor_visits',8192)):
        peak=receipt.get(peak_key);limit=receipt.get(limit_key)
        assert type(peak) is int and type(limit) is int and 0<=peak<=limit<=maximum, 'Capture exceeded '+limit_key
    max_worlds=receipt.get('max_worlds')
    assert type(max_worlds) is int and 0<max_worlds<=8, 'Invalid captured world limit'
    actors_path=path.with_suffix('.actors.csv')
    assert actors_path.is_file(), 'Missing actor capture CSV'
    with actors_path.open(encoding='utf-8-sig',newline='') as stream:
        actor_count=sum(1 for _ in csv.DictReader(stream))
    assert receipt.get('actor_rows')==actor_count, 'Actor CSV row count differs from completion metadata'
    return path,receipt,max_worlds


def validate_authority_encodes(path):
    """Authority model-encode observation, NOT client interpolation or per-connection sends."""
    path,receipt,max_worlds=read_completion(path)
    assert receipt.get('authority_encode_requested') is True, 'Authority encode observation was not requested'
    assert receipt.get('raw_receive_requested') is False, 'Authority capture masquerades as client receive'
    assert receipt.get('authority_encode_capable') is True and receipt.get('authority_encode_enabled') is True, 'Authority encode capability missing or disabled'
    assert receipt.get('authority_encode_thread_supported') is True, 'Unsupported authority encode thread'
    size=receipt.get('model_wire_snapshot_bytes')
    assert type(size) is int and 0<size<=64 and receipt.get('model_wire_max_tracked_snapshot_bytes')==size*4*1024, 'Invalid model-wire fixed snapshot bound'
    started=receipt.get('capture_started_seconds')
    assert type(started) in (float,int) and math.isfinite(started) and started>=0, 'Invalid authority capture clock'
    assert receipt.get('observation_clock') in ('WindowsQPC','ProcessMonotonic') and receipt.get('observation_host') and type(receipt.get('observation_process_id')) is int and receipt['observation_process_id']>0, 'Missing authority clock/host/process identity'
    worlds=defaultdict(list)
    with path.open(encoding='utf-8-sig',newline='') as stream:
        for row in csv.DictReader(stream):
            assert int(row['NetMode']) in (1,2), 'Authority capture contains a client/standalone world'
            values={k:float(v) for k,v in row.items() if k!='World'}
            assert all(math.isfinite(v) for v in values.values()), 'Non-finite authority world value'
            worlds[row['World']].append(values)
    assert sum(map(len,worlds.values()))==receipt['world_rows'] and 0<len(worlds)<=max_worlds, 'Authority world row count/bound mismatch'
    for rows in worlds.values():
        assert all(b['WallSeconds']>a['WallSeconds'] and b['SimulationMS']>=a['SimulationMS'] for a,b in zip(rows,rows[1:])), 'Authority world clock reset/rewound; do not join world lifetimes'
    world_at={(world,row['WallSeconds']):row['SimulationMS'] for world,rows in worlds.items() for row in rows}
    required={'WallSeconds','World','Actor','Role','PendingFrame','CachedSimulatedFrame','EncodeStatus','EncodeSimID','EncodeBindingEpoch','EncodeChannel','EncodeSerial','EncodeSeconds','EncodeEngineFrame','EncodeTimeKind','EncodeServerTimeMS'}
    streams={};row_count=encoded_count=0
    with path.with_suffix('.actors.csv').open(encoding='utf-8-sig',newline='') as stream:
        reader=csv.DictReader(stream)
        assert required.issubset(reader.fieldnames or []), 'Missing authority encode columns'
        for row in reader:
            row_count+=1
            assert int(row['Role'])==3 and row['EncodeChannel']=='SP' and row['World'] in worlds, 'Authority role/world/channel mismatch'
            status=row['EncodeStatus']
            assert status in ('AwaitingEncode','BeforeCapture','AuthorityEncoded'), 'Incomplete authority source: '+status
            sim=int(row['EncodeSimID']);epoch=int(row['EncodeBindingEpoch']);serial=int(row['EncodeSerial'])
            assert sim>0 and 0<epoch<=2**64-1 and 0<=serial<=2**64-1, 'Invalid authority binding identity/counter'
            key=(row['World'],row['Actor'],sim,epoch)
            record=streams.setdefault(key,dict(samples=0,encoded_rows=0,coalesced_encodes=0,repeated_wire_stamps=0,last=None,max_source_ahead_of_cached_frame=None))
            record['samples']+=1
            pending=int(row['PendingFrame']);cached=int(row['CachedSimulatedFrame'])
            if pending>=0 and cached>=0:
                ahead=pending-cached;old=record['max_source_ahead_of_cached_frame']
                record['max_source_ahead_of_cached_frame']=ahead if old is None else max(old,ahead)
            if status=='AwaitingEncode':
                assert serial==0 and int(row['EncodeTimeKind'])==0, 'Awaiting authority contains an encoded event'
                continue
            seconds=float(row['EncodeSeconds']);wall=float(row['WallSeconds']);frame=int(row['EncodeEngineFrame']);stamp=int(row['EncodeServerTimeMS'])
            assert serial>0 and frame>=0 and int(row['EncodeTimeKind'])==1 and stamp>=0, 'Authority encode has wrong time kind/value'
            assert math.isfinite(seconds) and math.isfinite(wall), 'Non-finite authority event clock'
            if status=='BeforeCapture':
                assert seconds<started, 'Authority BeforeCapture event is not old'
                continue
            assert started<=seconds<=started+wall+.001, 'Encode event outside its capture/row clock'
            assert (row['World'],wall) in world_at and stamp<=world_at[(row['World'],wall)], 'Encoded SP time is ahead of its server-variable source clock'
            encoded_count+=1;record['encoded_rows']+=1
            current=dict(serial=serial,encode_seconds=seconds,engine_frame=frame,server_time_ms=stamp)
            previous=record['last']
            if previous:
                assert serial>=previous['serial'] and seconds>=previous['encode_seconds'] and stamp>=previous['server_time_ms'], 'Authority event clock/serial rewound within a binding'
                if serial==previous['serial']:
                    assert current==previous, 'Encode value changed without a model encode'
                else:
                    record['coalesced_encodes']+=max(0,serial-previous['serial']-1)
                    record['repeated_wire_stamps']+=stamp==previous['server_time_ms']
            record['last']=current
    assert row_count==receipt.get('authority_rows'), 'Authority row count mismatch'
    assert encoded_count>0 and encoded_count==receipt.get('authority_encoded_rows'), 'No current-session authority encode rows or count mismatch'
    return dict(semantics='Authority SP model encoding; not per-connection actual sends or packets.',snapshot_bytes=size,
                observation_clock=receipt.get('observation_clock'),observation_host=receipt.get('observation_host'),
                rows=row_count,encoded_rows=encoded_count,worlds=[dict(world=w,samples=len(rows),first_sim_ms=rows[0]['SimulationMS'],last_sim_ms=rows[-1]['SimulationMS']) for w,rows in worlds.items()],
                streams=[dict(world=k[0],actor=k[1],sim_id=k[2],binding_epoch=k[3],**v) for k,v in streams.items()])


def join_authority_receives(authority_path,receive_path):
    """Same-host QPC boundary evidence. Never computes exact packet/one-way latency."""
    authority_path=Path(authority_path);receive_path=Path(receive_path)
    authority=validate_authority_encodes(authority_path)
    validate_raw_receives(receive_path)
    _,ar,_=read_completion(authority_path);_,rr,_=read_completion(receive_path)
    assert ar.get('observation_clock')==rr.get('observation_clock')=='WindowsQPC', 'Cross-process join requires Windows QPC'
    assert ar.get('observation_host') and ar.get('observation_host')==rr.get('observation_host'), 'Cross-host join is unsupported'
    assert type(ar.get('observation_process_id')) is int and type(rr.get('observation_process_id')) is int and 0<ar['observation_process_id']!=rr['observation_process_id']>0, 'Distinct observed process identities required'
    events=defaultdict(dict);epochs=defaultdict(set)
    with authority_path.with_suffix('.actors.csv').open(encoding='utf-8-sig',newline='') as stream:
        for row in csv.DictReader(stream):
            if row['EncodeStatus']!='AuthorityEncoded':continue
            key=(row['World'],int(row['EncodeSimID']));epoch=int(row['EncodeBindingEpoch']);serial=int(row['EncodeSerial'])
            epochs[key].add(epoch)
            events[key][(epoch,serial)]=(float(row['EncodeSeconds']),int(row['EncodeServerTimeMS']))
    timeline={k:sorted(v.values()) for k,v in events.items()}
    event_times={k:[e[0] for e in v] for k,v in timeline.items()}
    stamp_counts={k:Counter(stamp for _,stamp in v) for k,v in timeline.items()}
    counters=Counter();streams={}
    with receive_path.with_suffix('.actors.csv').open(encoding='utf-8-sig',newline='') as stream:
        for row in csv.DictReader(stream):
            if row['RawStatus']!='Received' or row['RawChannel']!='SP' or int(row['RawTimeKind'])!=1:
                counters['non_sp_or_uninitialized_rows']+=1;continue
            key=(row['World'],int(row['RawSimID']));stream_key=(*key,int(row['RawBindingEpoch']))
            counts=streams.setdefault(stream_key,Counter());counters['sp_rows']+=1;counts['sp_rows']+=1
            if key not in timeline:
                counters['missing_authority_rows']+=1;counts['missing_authority_rows']+=1;continue
            if len(epochs[key])!=1:
                counters['ambiguous_authority_epoch_rows']+=1;counts['ambiguous_authority_epoch_rows']+=1;continue
            sample=rr['capture_started_seconds']+float(row['WallSeconds'])
            index=bisect_right(event_times[key],sample)-1
            if index<0:
                counters['no_sampled_encode_before_row']+=1;counts['no_sampled_encode_before_row']+=1;continue
            _,encoded_stamp=timeline[key][index];received_stamp=int(row['RawTimeMS'])
            if encoded_stamp>received_stamp:
                counters['observed_post_encode_supply_gap_rows']+=1;counts['observed_post_encode_supply_gap_rows']+=1
                if float(row['RawWorldInterpolationMS'])-float(row['RawViewTimeMS'])>100:
                    counters['lag_gt100_post_encode_supply_gap_rows']+=1;counts['lag_gt100_post_encode_supply_gap_rows']+=1
            elif encoded_stamp<received_stamp:
                counters['receiver_ahead_of_sampled_encode_rows']+=1;counts['receiver_ahead_of_sampled_encode_rows']+=1
            else:
                counters['latest_observed_stamp_equal_rows']+=1;counts['latest_observed_stamp_equal_rows']+=1
            matches=stamp_counts[key][received_stamp]
            label='missing_sampled_stamp_rows' if matches==0 else 'ambiguous_repeated_stamp_rows' if matches>1 else 'single_sampled_stamp_rows'
            counters[label]+=1;counts[label]+=1
    assert counters['sp_rows']>0, 'No SP server-variable samples available for join'
    return dict(semantics='Same-host QPC model-encode/receive boundary evidence; coalesced latest values, no exact one-way latency or packet-send claim.',
                observation_host=ar['observation_host'],counters=dict(counters),
                streams=[dict(world=k[0],sim_id=k[1],receive_epoch=k[2],**v) for k,v in streams.items()],
                limits='Missing encoded stamps may be coalesced, and repeated stamps are ambiguous. Different endpoint epochs are not required to match. Source/Pending/Cached frame witnesses remain authority-local.')


def validate(path, require_hitch_ms=0.0, min_receive_hz=1.0, require_raw_receives=False):
    path,receipt,max_worlds=read_completion(path)
    worlds=defaultdict(list)
    observed_worlds=set()
    world_count=0
    with path.open(encoding='utf-8-sig',newline='') as stream:
        for source in csv.DictReader(stream):
            world_count+=1
            observed_worlds.add(source['World'])
            row={key:float(value) for key,value in source.items() if key!='World'}
            if row['NetMode']==3:
                assert all(math.isfinite(value) for value in row.values()), 'Non-finite client sample'
                worlds[source['World']].append(row)
    assert receipt.get('world_rows')==world_count, 'World CSV row count differs from completion metadata'
    assert len(observed_worlds)<=max_worlds, 'Capture exceeded distinct world limit'
    assert worlds, 'No initialized client interpolation stream was captured'
    results=[]
    for name,rows in worlds.items():
        all_rows=rows
        initialized=lambda row: row['LatestReceiveMS']>row['DesiredMS'] and row['InterpolationMS']>0
        first=next((index for index,row in enumerate(rows) if initialized(row)),None)
        assert first is not None, 'Client stream never initialized: '+name
        rows=rows[first:]
        assert all(initialized(row) for row in rows), 'Client stream reset or became invalid after initialization: '+name
        assert all(b['WallSeconds']>a['WallSeconds'] for a,b in zip(rows,rows[1:])), 'Capture clock did not advance: '+name
        assert len(rows)>=100, 'Insufficient client samples: '+name
        assert all(row['BufferedMS']>=-.01 for row in rows), 'Presentation exceeded received history: '+name
        assert all(row['InterpolationMS']<=row['LatestReceiveMS']+.01 for row in rows), 'Future state consumed: '+name
        assert all(b['InterpolationMS']>=a['InterpolationMS']-.01 for a,b in zip(rows,rows[1:])), 'Presentation rewound: '+name
        largest=max(row['WorldDeltaMS'] for row in rows)
        assert largest>=require_hitch_ms, 'Requested hitch stress was not exercised: '+name
        # Holding received history is safe but not proof of healthy buffering.
        # Require recovery in the final settled window as a separate condition.
        tail=[row for row in rows if row['WallSeconds']>=rows[-1]['WallSeconds']-3]
        tail_mean=sum(row['BufferedMS'] for row in tail)/len(tail)
        # A 15Hz snapshot stream naturally has lower troughs than a 120Hz PIE
        # stream. Measure recovery across the packet cycle, not only its trough.
        assert tail_mean>=tail[-1]['DesiredMS']*.5, 'Buffer did not recover across the receive cycle: '+name
        duration=tail[-1]['WallSeconds']-tail[0]['WallSeconds']
        receive_hz=sum(row['ReceiveAdvanceMS']>0 for row in tail)/max(.001,duration)
        assert receive_hz>=max(1.0,min_receive_hz), 'Snapshot supply stopped or is slower than the requested contract: '+name
        assert len({(row['DesiredMS'],row['MaxMS']) for row in rows})==1, 'Buffer settings changed during capture'
        results.append({'world':name,'frames':len(rows),'total_frames':len(all_rows),'warmup_frames':first,
                        'total_duration_seconds':all_rows[-1]['WallSeconds']-all_rows[0]['WallSeconds'],
                        'measured_duration_seconds':rows[-1]['WallSeconds']-rows[0]['WallSeconds'],
                        'min_buffer_ms':min(row['BufferedMS'] for row in rows),
                        'max_buffer_ms':max(row['BufferedMS'] for row in rows),'max_frame_ms':largest,
                        'tail_min_buffer_ms':min(row['BufferedMS'] for row in tail),'tail_mean_buffer_ms':tail_mean,
                        'tail_receive_hz':receive_hz,'future_frames':0,'rewinds':0})
    result={'passed':True,'capture':str(path),'completion':receipt,'worlds':results}
    if require_raw_receives or receipt.get('raw_receive_requested') is True:
        result['raw_receives']=validate_raw_receives(path)
    return result



DECODED_BASIS = 'MoverStoredBaseOrWorldXYVelocityXYFloat32'
DECODED_FIELDS = ('RawPosX','RawPosY','RawVelX','RawVelY')


def analyze_decoded_rows(rows, expected_ids):
    """Current latest value samples. A serial gap is explicitly missing evidence."""
    assert 1 <= len(expected_ids) <= 3 and len(set(expected_ids)) == len(expected_ids), 'Expected1..3 unique actor IDs'
    assert all(str(int(i)) == i and 0 < int(i) <= 2**64-1 for i in expected_ids), 'Invalid selected Iris identity'
    streams = {}
    for row in rows:
        actor = row['ActorId']
        assert actor in expected_ids and row['Role'] == '1' and row['RawChannel'] == 'SP', 'Wrong focused actor or role'
        assert row['RawScalarBasis'] == DECODED_BASIS, 'Unknown decoded coordinate contract'
        status = row['RawStatus']
        if status != 'Received':
            assert status in ('AwaitingReceive','BeforeCapture'), 'Incomplete focused receive source: '+status
            continue
        assert row['RawScalarsValid'] == '1', 'Decoded scalar projection unavailable'
        values = tuple(float(row[f]) for f in DECODED_FIELDS)
        assert all(math.isfinite(v) for v in values), 'Nonfinite decoded scalar'
        world_s = float(row['ObservationWorldSeconds'])
        actor_world = tuple(float(row[f]) for f in ('ActorWorldX','ActorWorldY','PublishedWorldVelX','PublishedWorldVelY'))
        assert math.isfinite(world_s) and all(math.isfinite(v) for v in actor_world), 'Nonfinite Actor observation'
        state = streams.setdefault(actor, dict(world=row['World'], samples=0, observed_decode_serials=0, coalesced_unobserved_decodes=0,
            raw_negative_vx_decodes=0, consumed_negative_vx_samples=0, presentation_negative_vx_samples=0,
            actor_negative_vx_samples=0, actor_negative_dx_samples=0, consumed_unavailable_samples=0,
            presentation_unavailable_samples=0, last=None, last_actor_x=None))
        identity = (int(row['RawSimID']), int(row['RawBindingEpoch']))
        assert row['World'] == state['world'], 'Selected identity crossed world contexts'
        current = (identity, int(row['RawApplySerial']), int(row['RawTimeMS']), values)
        assert identity[0] > 0 and identity[1] > 0 and current[1] > 0, 'Invalid decode identity'
        previous = state['last']
        if previous:
            assert identity == previous[0], 'Selected Actor changed NP binding during observation'
            assert current[1] >= previous[1], 'Decode serial regressed'
            if current[1] == previous[1]:
                assert current == previous, 'Same decode serial changed raw values or time'
            else:
                state['coalesced_unobserved_decodes'] += current[1]-previous[1]-1
        if not previous or current[1] != previous[1]:
            state['observed_decode_serials'] += 1
            state['raw_negative_vx_decodes'] += values[2] < -5
        for prefix, key in (('Consumed','consumed'), ('Presentation','presentation')):
            valid = row[prefix+'Valid']
            assert valid in ('0','1'), 'Invalid view validity flag'
            assert row[prefix+'BaseResolved'] in ('0','1'), 'Invalid current-view base resolution flag'
            if valid == '1':
                view = tuple(float(row[prefix+f]) for f in ('PosX','PosY','VelX','VelY'))
                assert all(math.isfinite(v) for v in view), 'Nonfinite '+prefix+' values'
                state[key+'_negative_vx_samples'] += view[2] < -5
            else:
                state[key+'_unavailable_samples'] += 1
        state['actor_negative_vx_samples'] += actor_world[2] < -5
        if state['last_actor_x'] is not None:
            state['actor_negative_dx_samples'] += actor_world[0] < state['last_actor_x']
        state['last_actor_x'] = actor_world[0]
        state['last'] = current
        state['samples'] += 1
    assert set(streams) == set(expected_ids), 'A selected actor has no current decoded row'
    assert len({s['world'] for s in streams.values()}) == 1, 'Focused capture crossed worlds'
    for state in streams.values():
        assert state['samples'] > 0 and state['observed_decode_serials'] > 0, 'No decoded evidence'
        state['intermediate_decode_values_missing'] = state['coalesced_unobserved_decodes'] > 0
    return streams


def validate_decoded_endpoints(path, selection_path=None):
    path, receipt, _ = read_completion(path)
    assert receipt.get('stop_reason') == 'ExplicitStop', 'Focused capture did not close at the explicit phase4 boundary'
    raw = validate_raw_receives(path)
    expected = receipt.get('focused_actor_ids', [])
    assert receipt.get('focused_actor_count') == len(expected), 'Filter count differs'
    assert receipt.get('focused_observed_mask') == (1 << len(expected))-1, 'Selected actor was never observed'
    assert receipt.get('decoded_scalar_contract') == DECODED_BASIS, 'Unknown snapshot scalar contract'
    assert receipt.get('decoded_basis_world_proven') is False, 'Stored decode coordinates mislabelled as world'
    if selection_path:
        selection = json.loads(Path(selection_path).read_text(encoding='utf-8-sig'))
        assert selection.get('selection_phase') == 3 and selection.get('observation_phase') == 4, 'Wrong selection phases'
        assert [r['actor_id'] for r in selection['selected']] == expected, 'Runtime selection/filter identity mismatch'
    with path.with_suffix('.actors.csv').open(encoding='utf-8-sig',newline='') as source:
        streams = analyze_decoded_rows(csv.DictReader(source), expected)
    return dict(observation_valid=True, capture=str(path), decoded_basis=DECODED_BASIS,
        basis_world_proven=False, raw=raw, streams=streams,
        semantics='Same post-codec payload projected to float32 stored-basis PositionXY/VelocityXY. Consumed is the current public pending endpoint. Presentation is the independently owned service output when available, with pending fallback on unsupported views. Historical captures can retain a prior interpolant during endpoint hold; the current service publishes its selected held endpoint. The sampled view need not equal the current Actor/published velocity at another update boundary. Latest-only samples and serial gaps omit intermediate observed codec-event values, not packets lost in transit; equal times never prove raw/view identity. Current weak base resolution does not reveal the original decoded base-use bit. No workload pass override.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--require-hitch-ms',type=float,default=0.)
    parser.add_argument('--min-receive-hz',type=float,default=1.)
    parser.add_argument('--require-raw-receives',action='store_true')
    parser.add_argument('--authority-encodes',action='store_true',help='Validate a separately typed authority observation; never applies client interpolation checks to server data')
    parser.add_argument('--authority-capture',type=Path,help='Join a client receive file to the same-host QPC authority model-encode capture')
    parser.add_argument('--raw-report',type=Path,help='Write raw-value evidence before unchanged strict world/log checks; never changes pass criteria')
    parser.add_argument('--decoded-endpoints',action='store_true',help='Validate only bounded optional decoded-value evidence; never overrides the workload/interpolation gates')
    parser.add_argument('--selection',type=Path,help='Join the actual phase3 selected Iris IDs to this focused capture')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--log',type=Path,help='Require the explicit ready-stage capture marker and reject all later NP diagnostics')
    args=parser.parse_args()
    if args.decoded_endpoints:
        assert not args.authority_encodes and not args.authority_capture and not args.require_raw_receives and not args.raw_report, 'Focused evidence is a separate observation mode'
        result=validate_decoded_endpoints(args.capture,args.selection)
    elif args.authority_encodes:
        assert not args.authority_capture and not args.require_raw_receives, 'Authority mode cannot masquerade as client receive'
        observation=validate_authority_encodes(args.capture)
        result={'observation_valid':True,'capture':str(args.capture),'authority_encodes':observation}
    else:
        observation=validate_raw_receives(args.capture) if args.raw_report or args.authority_capture else None
        if args.authority_capture:
            observation['authority_join']=join_authority_receives(args.authority_capture,args.capture)
        # Evidence remains available even when the unchanged strict client checks fail below.
        if args.raw_report:args.raw_report.write_text(json.dumps(observation,indent=2),encoding='utf-8')
        result=validate(args.capture,args.require_hitch_ms,args.min_receive_hz,args.require_raw_receives)
    if args.authority_encodes and args.raw_report:
        args.raw_report.write_text(json.dumps(observation,indent=2),encoding='utf-8')
    if args.log:result['diagnostics']=validate_diagnostics(args.log)
    encoded=json.dumps(result,ensure_ascii=False,indent=2)
    if args.output:args.output.write_text(encoded,encoding='utf-8')
    print(encoded)
