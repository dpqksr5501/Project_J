"""Real CSV and disposable Git regressions for the Mover audit tools."""
import csv
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


buffer = module('mover_buffer', 'Scripts/Validation/Validate-MoverBufferCapture.py')
provenance = module('continuation_provenance', 'Scripts/Audit/continuation_provenance.py')


class MoverAuditToolsTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'Saved/Validation/MoverAudit/ToolTests'
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def capture(self, fault=None):
        rows = []
        for i in range(300):
            row = dict(World='Client', NetMode=3, WallSeconds=i / 60, LatestReceiveMS=200+i*16,
                       DesiredMS=100, InterpolationMS=100+i*16, BufferedMS=100,
                       WorldDeltaMS=16, ReceiveAdvanceMS=16, MaxMS=300)
            if i < 5 or (fault == 'late_reset' and i >= 250) or (fault == 'middle_reset' and i == 150):
                row.update(LatestReceiveMS=0, InterpolationMS=0)
            if fault == 'stopped':
                row.update(LatestReceiveMS=200, InterpolationMS=100, ReceiveAdvanceMS=0)
            rows.append(row)
        path = self.work / 'capture.csv'
        with path.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        path.with_suffix('.actors.csv').write_text('Actor,Role\nPawn,2\n',encoding='utf-8')
        self.receipt=dict(schema_version=1,complete=True,truncated=False,stop_reason='Duration',
                          csv_saved=True,actors_csv_saved=True,world_rows=len(rows),actor_rows=1,
                          peak_retained_bytes=16*1024*1024,max_retained_bytes=18*1024*1024,
                          peak_components=1,max_components=1024,peak_discovery_actor_visits=10,
                          max_discovery_actor_visits=8192,max_worlds=8)
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        return path

    def raw_capture(self, channel='SP'):
        path=self.capture()
        rows=[]
        for index in range(3):
            rows.append(dict(World='Client',Actor='Pawn',Role=1 if channel=='SP' else 2,WallSeconds=index/30,
                RawStatus='Received',RawSimID=7,RawBindingEpoch=3,RawChannel=channel,RawApplySerial=1+index*2,
                RawDecodeSeconds=10+index/30,RawDecodeEngineFrame=100+index,
                RawFrameKind=0 if channel=='SP' else 2,RawFrameValue=-1 if channel=='SP' else 70+index,
                RawTimeKind=1 if channel=='SP' else 2,RawTimeMS=1000+index*30,RawViewTimeMS=800,RawWorldInterpolationMS=900))
        with path.with_suffix('.actors.csv').open('w',encoding='utf-8',newline='') as file:
            writer=csv.DictWriter(file,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
        self.receipt.update(actor_rows=3,raw_receive_requested=True,raw_receive_capable=True,raw_receive_enabled=True,
            raw_receive_thread_supported=True,raw_receive_client_rows=3,raw_receive_received_rows=3,
            raw_receive_snapshot_bytes=48,raw_receive_max_tracked_snapshot_bytes=48*4*1024,capture_started_seconds=10)
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        return path,rows

    def write_raw_rows(self,path,rows):
        with path.with_suffix('.actors.csv').open('w',encoding='utf-8',newline='') as file:
            writer=csv.DictWriter(file,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)

    def decoded_capture(self, actor_ids=('1556',)):
        # Reuse the real CSV/receipt writers; these are parser fixtures, not actor simulations.
        path,seed=self.raw_capture();rows=[]
        for actor_index,actor_id in enumerate(actor_ids):
            for index,source in enumerate(seed):
                row=dict(source,Actor='Pawn_'+actor_id,ActorId=actor_id,RawSimID=10+actor_index,RawApplySerial=index+1,
                    ObservationWorldSeconds=index/30,RawScalarBasis=buffer.DECODED_BASIS,RawScalarsValid=1,
                    ConsumedValid=1,PresentationValid=1,ConsumedBaseResolved=0,PresentationBaseResolved=0,
                    ActorWorldX=20-index,ActorWorldY=actor_index,PublishedWorldVelX=500,PublishedWorldVelY=0)
                for prefix in ('Raw','Consumed','Presentation'):
                    row.update({prefix+'PosX':20-index,prefix+'PosY':actor_index,prefix+'VelX':500,prefix+'VelY':0})
                rows.append(row)
        self.write_raw_rows(path,rows)
        self.receipt.update(stop_reason='ExplicitStop',actor_rows=len(rows),peak_components=len(actor_ids),
            raw_receive_client_rows=len(rows),raw_receive_received_rows=len(rows),
            raw_receive_snapshot_bytes=64,raw_receive_max_tracked_snapshot_bytes=64*4*1024,
            focused_actor_count=len(actor_ids),focused_observed_mask=(1<<len(actor_ids))-1,focused_actor_ids=list(actor_ids),
            decoded_scalar_contract=buffer.DECODED_BASIS,decoded_basis_world_proven=False)
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        selection=self.work/'selection.json'
        selection.write_text(json.dumps(dict(selection_phase=3,observation_phase=4,
            selected=[dict(actor_id=actor_id) for actor_id in actor_ids])),encoding='utf-8')
        return path,rows,selection

    def test_decoded_endpoint_csv_accepts_three_uint64_identities_without_world_claim(self):
        identities=('1','2','18446744073709551615')
        path,_,selection=self.decoded_capture(identities)
        report=buffer.validate_decoded_endpoints(path,selection)
        self.assertTrue(report['observation_valid']);self.assertFalse(report['basis_world_proven'])
        self.assertEqual(set(report['streams']),set(identities))
        for stream in report['streams'].values():
            self.assertEqual(stream['observed_decode_serials'],3)
            self.assertEqual(stream['raw_negative_vx_decodes'],0)
            self.assertEqual(stream['actor_negative_dx_samples'],2)
            self.assertFalse(stream['intermediate_decode_values_missing'])

    def test_decoded_serial_coalescing_is_unobserved_values_and_equal_time_is_not_identity(self):
        path,rows,selection=self.decoded_capture()
        rows[1].update(RawApplySerial=4,RawTimeMS=rows[0]['RawTimeMS']);rows[2]['RawApplySerial']=5
        self.write_raw_rows(path,rows);report=buffer.validate_decoded_endpoints(path,selection)
        stream=report['streams']['1556']
        self.assertEqual(stream['observed_decode_serials'],3)
        self.assertEqual(stream['coalesced_unobserved_decodes'],2)
        self.assertTrue(stream['intermediate_decode_values_missing'])
        self.assertEqual(report['raw']['streams'][0]['wire_time_repeats'],1)
        # Existing metadata is unchanged; only decoded values differ under the SAME serial.
        for key in ('RawApplySerial','RawTimeMS','RawDecodeSeconds','RawDecodeEngineFrame'):
            rows[1][key]=rows[0][key]
        self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'Same decode serial changed raw values'):
            buffer.validate_decoded_endpoints(path,selection)

    def test_decoded_filter_receipt_rejects_capacity_duplicate_and_uint64_overflow(self):
        for identities in (('1','2','3','4'),('1','1'),('0',),('18446744073709551616',)):
            with self.subTest(identities=identities):
                path,_,selection=self.decoded_capture(identities)
                with self.assertRaises(AssertionError):
                    buffer.validate_decoded_endpoints(path,selection)

    def test_decoded_endpoint_rejects_retired_basis_role_and_partial_or_early_receipts(self):
        for fault,message in (('epoch','NP binding'),('basis','coordinate contract'),('nonfinite','Nonfinite decoded'),
                              ('role','Wrong focused actor or role'),('partial','Partial capture'),('duration','explicit phase4 boundary')):
            with self.subTest(fault=fault):
                path,rows,selection=self.decoded_capture()
                if fault=='epoch':rows[1]['RawBindingEpoch']=4
                elif fault=='basis':rows[0]['RawScalarBasis']='World'
                elif fault=='nonfinite':rows[0]['RawVelX']='nan'
                elif fault=='role':rows[0].update(Role=2,RawChannel='AP',RawFrameKind=2,RawFrameValue=70,RawTimeKind=2)
                elif fault=='partial':self.receipt.update(complete=False,truncated=True,stop_reason='ActorBufferLimit')
                elif fault=='duration':self.receipt['stop_reason']='Duration'
                self.write_raw_rows(path,rows)
                path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
                with self.assertRaisesRegex(AssertionError,message):
                    buffer.validate_decoded_endpoints(path,selection)

    def test_raw_sp_missing_frame_and_coalesced_model_applies(self):
        path,_=self.raw_capture()
        raw=buffer.validate(path,require_raw_receives=True)['raw_receives']
        self.assertEqual(raw['streams'][0]['coalesced_applies'],2)
        self.assertEqual(raw['streams'][0]['last']['frame_kind'],0)
        self.assertEqual(raw['streams'][0]['max_view_behind_world_ms'],100)

    def test_ap_input_ack_clock_is_not_compared_with_world_interpolation(self):
        path,_=self.raw_capture('AP')
        raw=buffer.validate_raw_receives(path)
        self.assertEqual(raw['streams'][0]['last']['frame_kind'],2)
        self.assertIsNone(raw['streams'][0]['max_view_behind_world_ms'])

    def test_raw_receive_requires_optin_and_thread_capability(self):
        with self.assertRaisesRegex(AssertionError,'not requested'):
            buffer.validate(self.capture(),require_raw_receives=True)
        path,_=self.raw_capture();self.receipt['raw_receive_thread_supported']=False
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'writer thread'):
            buffer.validate(path,require_raw_receives=True)

    def test_raw_sp_rejects_invented_server_frame_and_stale_decode(self):
        path,rows=self.raw_capture();rows[0]['RawFrameValue']=0;self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'guessed local/server frame'):
            buffer.validate_raw_receives(path)
        rows[0]['RawFrameValue']=-1;rows[0]['RawDecodeSeconds']=9.;self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'outside this capture'):
            buffer.validate_raw_receives(path)

    def test_raw_unsupported_binding_or_count_mismatch_cannot_pass(self):
        path,rows=self.raw_capture();rows[-1]['RawStatus']='UnsupportedDriver';self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'Incomplete receive source'):
            buffer.validate_raw_receives(path)
        rows[-1]['RawStatus']='Received';self.write_raw_rows(path,rows)
        self.receipt['raw_receive_received_rows']=4
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'count mismatch'):
            buffer.validate_raw_receives(path)

    def authority_capture(self):
        path=self.work/'authority.csv'
        worlds=[dict(World='Client',NetMode=1,WallSeconds=round(1+i/30,6),WorldDeltaMS=33,SimulationMS=1200+i*30,
                     LatestReceiveMS=0,InterpolationMS=0,BufferedMS=0,ReceiveGapMS=0,ReceiveAdvanceMS=0,DesiredMS=100,MaxMS=250) for i in range(3)]
        rows=[dict(World='Client',Actor='ServerPawn',Role=3,WallSeconds=round(1+i/30,6),PendingFrame=30+i,CachedSimulatedFrame=29+i,
                   EncodeStatus='AuthorityEncoded',EncodeSimID=7,EncodeBindingEpoch=5,EncodeChannel='SP',EncodeSerial=[1,2,4][i],
                   EncodeSeconds=9.99+i/30,EncodeEngineFrame=200+i,EncodeTimeKind=1,EncodeServerTimeMS=1100+i*30) for i in range(3)]
        for target,data in ((path,worlds),(path.with_suffix('.actors.csv'),rows)):
            with target.open('w',encoding='utf-8',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=data[0]);writer.writeheader();writer.writerows(data)
        receipt=dict(schema_version=1,complete=True,truncated=False,stop_reason='Duration',csv_saved=True,actors_csv_saved=True,
            actor_rows=3,world_rows=3,peak_retained_bytes=16*1024*1024,max_retained_bytes=18*1024*1024,
            peak_components=1,max_components=1024,peak_discovery_actor_visits=10,max_discovery_actor_visits=8192,max_worlds=8,
            raw_receive_requested=False,authority_encode_requested=True,authority_encode_capable=True,authority_encode_enabled=True,
            authority_encode_thread_supported=True,authority_rows=3,authority_encoded_rows=3,model_wire_snapshot_bytes=48,
            model_wire_max_tracked_snapshot_bytes=48*4*1024,capture_started_seconds=9.,observation_clock='WindowsQPC',
            observation_host='SameHost',observation_process_id=11)
        path.with_suffix('.summary.json').write_text(json.dumps(receipt),encoding='utf-8')
        return path,rows,receipt

    def set_receive_clock(self,path,host='SameHost',clock='WindowsQPC'):
        receipt=json.loads(path.with_suffix('.summary.json').read_text(encoding='utf-8'))
        receipt.update(observation_host=host,observation_clock=clock,observation_process_id=22)
        path.with_suffix('.summary.json').write_text(json.dumps(receipt),encoding='utf-8')

    def test_authority_capture_is_typed_and_retains_fixed_bounds(self):
        path,_,_=self.authority_capture();report=buffer.validate_authority_encodes(path)
        self.assertEqual(report['encoded_rows'],3)
        self.assertEqual(report['streams'][0]['coalesced_encodes'],1)
        self.assertEqual(report['streams'][0]['max_source_ahead_of_cached_frame'],1)
        with self.assertRaisesRegex(AssertionError,'No initialized client'):
            buffer.validate(path)
        with self.assertRaisesRegex(AssertionError,'not requested'):
            buffer.validate_raw_receives(path)

    def test_authority_role_capability_and_count_are_strict(self):
        path,rows,receipt=self.authority_capture();rows[0]['Role']=1;self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'role/world/channel'):
            buffer.validate_authority_encodes(path)
        rows[0]['Role']=3;self.write_raw_rows(path,rows);receipt['authority_encoded_rows']=4
        path.with_suffix('.summary.json').write_text(json.dumps(receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'count mismatch'):
            buffer.validate_authority_encodes(path)
        receipt['authority_encoded_rows']=3;receipt['authority_encode_thread_supported']=False
        path.with_suffix('.summary.json').write_text(json.dumps(receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'encode thread'):
            buffer.validate_authority_encodes(path)

    def test_same_host_qpc_offsets_join_without_invented_latency(self):
        receive,_=self.raw_capture();self.set_receive_clock(receive)
        authority,_,_=self.authority_capture()
        joined=buffer.join_authority_receives(authority,receive)
        self.assertEqual(joined['counters']['observed_post_encode_supply_gap_rows'],3)
        self.assertEqual(joined['counters']['missing_sampled_stamp_rows'],3)
        self.assertNotIn('latency_ms',joined)
        self.set_receive_clock(receive,host='OtherHost')
        with self.assertRaisesRegex(AssertionError,'Cross-host'):
            buffer.join_authority_receives(authority,receive)
        self.set_receive_clock(receive,clock='ProcessMonotonic')
        with self.assertRaisesRegex(AssertionError,'Windows QPC'):
            buffer.join_authority_receives(authority,receive)

    def test_repeated_encode_stamp_and_epoch_are_ambiguous(self):
        receive,received=self.raw_capture();self.set_receive_clock(receive)
        for row in received:row['RawTimeMS']=1100
        self.write_raw_rows(receive,received)
        authority,rows,_=self.authority_capture();rows[1]['EncodeServerTimeMS']=1100;self.write_raw_rows(authority,rows)
        joined=buffer.join_authority_receives(authority,receive)
        self.assertEqual(joined['counters']['ambiguous_repeated_stamp_rows'],3)
        rows[2]['EncodeBindingEpoch']=6;rows[2]['EncodeSerial']=1;self.write_raw_rows(authority,rows)
        joined=buffer.join_authority_receives(authority,receive)
        self.assertEqual(joined['counters']['ambiguous_authority_epoch_rows'],3)

    def test_authority_time_kind_and_source_clock_cannot_be_personal(self):
        path,rows,_=self.authority_capture();rows[0]['EncodeTimeKind']=2;self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'wrong time kind'):
            buffer.validate_authority_encodes(path)
        rows[0]['EncodeTimeKind']=1;rows[0]['EncodeServerTimeMS']=99999;self.write_raw_rows(path,rows)
        with self.assertRaisesRegex(AssertionError,'server-variable source clock'):
            buffer.validate_authority_encodes(path)

    def test_missing_completion_metadata_fails(self):
        path=self.capture();path.with_suffix('.summary.json').unlink()
        with self.assertRaisesRegex(AssertionError,'Missing capture completion'):
            buffer.validate(path)

    def test_partial_capture_cannot_pass_with_healthy_rows(self):
        path=self.capture();self.receipt.update(complete=False,truncated=True,stop_reason='ActorBufferLimit')
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'Partial capture'):
            buffer.validate(path)

    def test_failed_csv_save_cannot_pass(self):
        path=self.capture();self.receipt['actors_csv_saved']=False
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'CSV save failed'):
            buffer.validate(path)

    def test_world_csv_truncation_is_detected(self):
        path=self.capture();self.receipt['world_rows']+=1
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'World CSV row count'):
            buffer.validate(path)

    def test_actor_csv_truncation_is_detected(self):
        path=self.capture();path.with_suffix('.actors.csv').write_text('Actor,Role\n',encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'Actor CSV row count'):
            buffer.validate(path)

    def test_missing_actor_csv_is_detected(self):
        path=self.capture();path.with_suffix('.actors.csv').unlink()
        with self.assertRaisesRegex(AssertionError,'Missing actor capture'):
            buffer.validate(path)

    def test_retained_memory_overflow_cannot_pass(self):
        path=self.capture();self.receipt['peak_retained_bytes']=self.receipt['max_retained_bytes']+1
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'retained memory budget'):
            buffer.validate(path)

    def test_component_overflow_cannot_pass(self):
        path=self.capture();self.receipt['peak_components']=1025
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'max_components'):
            buffer.validate(path)

    def test_discovery_overflow_cannot_pass(self):
        path=self.capture();self.receipt['peak_discovery_actor_visits']=8193
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'max_discovery_actor_visits'):
            buffer.validate(path)

    def test_excess_worlds_cannot_pass(self):
        path=self.capture();self.receipt['max_worlds']=1
        path.with_suffix('.summary.json').write_text(json.dumps(self.receipt),encoding='utf-8')
        contents=path.read_text(encoding='utf-8').replace('Client,3,','Other,3,',1)
        path.write_text(contents,encoding='utf-8')
        with self.assertRaisesRegex(AssertionError,'distinct world limit'):
            buffer.validate(path)

    def test_healthy_capture_preserves_warmup_and_duration(self):
        result = buffer.validate(self.capture())['worlds'][0]
        self.assertEqual(result['warmup_frames'], 5)
        self.assertEqual(result['total_frames'], 300)
        self.assertAlmostEqual(result['total_duration_seconds'], 299 / 60)

    def test_stopped_supply_fails_with_default_options(self):
        with self.assertRaisesRegex(AssertionError, 'supply stopped'):
            buffer.validate(self.capture('stopped'))

    def test_late_reset_is_not_warmup(self):
        with self.assertRaisesRegex(AssertionError, 'reset or became invalid'):
            buffer.validate(self.capture('late_reset'))

    def test_middle_reset_is_not_filtered(self):
        with self.assertRaisesRegex(AssertionError, 'reset or became invalid'):
            buffer.validate(self.capture('middle_reset'))

    def test_ready_boundary_preserves_startup_diagnostics(self):
        path=self.work/'Run.log'
        path.write_text('LogNetworkPrediction: Warning: initial load\nMoverBufferCapture Started Seconds=120\nhealthy\n')
        result=buffer.validate_diagnostics(path)
        self.assertEqual(len(result['startup_diagnostics']),1)
        self.assertEqual(result['captured_diagnostics'],[])

    def test_diagnostic_after_capture_start_fails(self):
        path=self.work/'Run.log'
        path.write_text('MoverBufferCapture Started Seconds=120\nLogNetworkPrediction: Warning: late failure\n')
        with self.assertRaisesRegex(AssertionError,'during the captured stream'):
            buffer.validate_diagnostics(path)

    def test_missing_ready_capture_marker_fails(self):
        path=self.work/'Run.log';path.write_text('healthy but never captured\n')
        with self.assertRaisesRegex(AssertionError,'No explicit'):
            buffer.validate_diagnostics(path)

    def git_fixture(self):
        subprocess.run(['git', 'init', '-q', str(self.work)], check=True)
        for path in ('Source/Game.cpp', 'Plugins/Mover/Source/Mode.cpp',
                     'Plugins/NetworkPrediction/NetworkPrediction.uplugin'):
            full = self.work / path; full.parent.mkdir(parents=True, exist_ok=True); full.write_text('original')
        subprocess.run(['git','-C',str(self.work),'add','.'],check=True)
        return provenance.source_hashes(self.work)

    def test_unchanged_provenance_matches(self):
        frozen = self.git_fixture()
        self.assertEqual(provenance.compare_sources(self.work, frozen), [])

    def test_mover_and_network_prediction_changes_are_rejected(self):
        frozen = self.git_fixture()
        for path in ('Plugins/Mover/Source/Mode.cpp', 'Plugins/NetworkPrediction/NetworkPrediction.uplugin'):
            (self.work / path).write_text('changed')
        self.assertEqual(len(provenance.compare_sources(self.work, frozen)), 2)

    def test_new_plugin_source_is_rejected(self):
        frozen = self.git_fixture()
        (self.work / 'Plugins/Mover/Source/Added.cpp').write_text('new')
        self.assertEqual(len(provenance.compare_sources(self.work, frozen)), 1)

    def test_plugin_binary_changes_are_rejected(self):
        self.git_fixture()
        path = self.work / 'Plugins/Mover/Binaries/Win64/UnrealEditor-Mover.dll'
        path.parent.mkdir(parents=True); path.write_bytes(b'old')
        frozen = provenance.plugin_artifacts(self.work)
        path.write_bytes(b'new')
        self.assertEqual(provenance.check_artifacts(self.work, frozen), [path.relative_to(self.work).as_posix()])

    def powershell_provenance(self, mutation):
        powershell = shutil.which('pwsh')
        if not powershell:
            self.skipTest('PowerShell 7 is required to exercise the actual provenance collector/comparator')
        self.git_fixture()
        for relative, contents in {
            '.gitignore': '**/Binaries/\n',
            'Config/Default.ini': 'fixture config',
            'Scripts/Tool.py': 'fixture script',
            'Source/Nested/Zed.cpp': 'nested source',
            'Project_J.uproject': '{}',
        }.items():
            path = self.work / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        subprocess.run(['git', '-C', str(self.work), 'add', '.'], check=True)
        hooks = self.work / 'EmptyHooks'; hooks.mkdir()
        subprocess.run(['git', '-C', str(self.work), '-c', 'user.name=ProvenanceFixture',
                        '-c', 'user.email=fixture@example.invalid', '-c', 'commit.gpgsign=false',
                        '-c', f'core.hooksPath={hooks}', 'commit', '-qm', 'fixture'], check=True)
        for relative in ('Binaries/Win64/UnrealEditor-Project_J.dll',
                         'Binaries/Win64/UnrealEditor.modules',
                         'Plugins/Mover/Binaries/Win64/UnrealEditor-Mover.dll',
                         'EngineHost/UnrealEditor-Cmd.exe'):
            path = self.work / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture artifact; never executed')
        script = self.work / 'Compare.ps1'
        script.write_text(r'''
param([string]$ProjectRoot,[string]$Collector,[string]$Mutation)
$ErrorActionPreference='Stop'
. $Collector
$live=Get-ProjectJSourceProvenance -ProjectRoot $ProjectRoot -RuntimePaths @("$ProjectRoot/EngineHost/UnrealEditor-Cmd.exe")
$saved=$live|ConvertTo-Json -Depth 12|ConvertFrom-Json
$liveType=$live.files[0].GetType().FullName
$savedType=$saved.files[0].GetType().FullName
if($Mutation -eq 'roundtrip'){
 Assert-ProjectJMatchingProvenance -Provenance @($live,$saved)
 # Capture and JSON representation need not retain the same row order.
 $reordered=[ordered]@{schema=$live.schema;head=$live.head
  files=@($live.files|Sort-Object -Property {[string]$_.path} -Descending)
  binaries=@($live.binaries|Sort-Object -Property {[string]$_.path} -Descending)}
 Assert-ProjectJMatchingProvenance -Provenance @($saved,$reordered)
 @{matched=$true;liveType=$liveType;savedType=$savedType;orderIndependent=$true}|ConvertTo-Json -Compress
 exit 0
}
switch($Mutation){
 'file_hash' {Set-Content -LiteralPath "$ProjectRoot/Source/Game.cpp" -Value 'changed source'}
 'file_added' {Set-Content -LiteralPath "$ProjectRoot/Source/Added.cpp" -Value 'new untracked source'}
 'file_deleted' {Remove-Item -LiteralPath "$ProjectRoot/Source/Game.cpp"}
 'plugin_binary' {Set-Content -LiteralPath "$ProjectRoot/Plugins/Mover/Binaries/Win64/UnrealEditor-Mover.dll" -Value 'changed binary'}
 default {throw 'Unknown fixture mutation'}
}
$after=Get-ProjectJSourceProvenance -ProjectRoot $ProjectRoot -RuntimePaths @("$ProjectRoot/EngineHost/UnrealEditor-Cmd.exe")
$failure=$null
try{Assert-ProjectJMatchingProvenance -Provenance @($saved,$after)}catch{$failure=$_.Exception.Message}
if($failure -ne 'Network comparisons used different HEAD/source/configuration/binary provenance.'){
 throw "Expected an actual content/provenance mismatch, got: $failure"
}
$pluginPath='Plugins/Mover/Binaries/Win64/UnrealEditor-Mover.dll'
@{rejected=$true;error=$failure;liveType=$liveType;savedType=$savedType
 tombstone=@($after.files|Where-Object {$_.path -eq 'Source/Game.cpp' -and $_.deleted}).Count -eq 1
 pluginBinaryOnly=@($after.files|Where-Object {$_.path -eq $pluginPath}).Count -eq 0 -and
                  @($after.binaries|Where-Object {$_.path -eq $pluginPath}).Count -eq 1}|ConvertTo-Json -Compress
''', encoding='utf-8')
        result = subprocess.run([powershell, '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(script),
                                 '-ProjectRoot', str(self.work), '-Collector', str(ROOT / 'Scripts/Validation/Source-Provenance.ps1'),
                                 '-Mutation', mutation], capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        receipt = json.loads(result.stdout)
        self.assertEqual(receipt['liveType'], 'System.Collections.Specialized.OrderedDictionary')
        self.assertEqual(receipt['savedType'], 'System.Management.Automation.PSCustomObject')
        return receipt

    def test_powershell_live_and_json_provenance_are_identical(self):
        receipt = self.powershell_provenance('roundtrip')
        self.assertTrue(receipt['matched'])
        self.assertTrue(receipt['orderIndependent'])

    def test_powershell_rejects_file_hash_change(self):
        self.assertTrue(self.powershell_provenance('file_hash')['rejected'])

    def test_powershell_rejects_new_untracked_source(self):
        self.assertTrue(self.powershell_provenance('file_added')['rejected'])

    def test_powershell_rejects_deleted_tracked_source(self):
        receipt = self.powershell_provenance('file_deleted')
        self.assertTrue(receipt['rejected'])
        self.assertTrue(receipt['tombstone'])

    def test_powershell_rejects_plugin_binary_change(self):
        receipt = self.powershell_provenance('plugin_binary')
        self.assertTrue(receipt['rejected'])
        self.assertTrue(receipt['pluginBinaryOnly'])

    def test_content_worktree_changes_are_not_hidden_by_source_identity(self):
        self.git_fixture()
        path=self.work/'Content/CurrentMaster.uasset';path.parent.mkdir();path.write_bytes(b'first')
        frozen=provenance.content_worktree_hashes(self.work)
        self.assertEqual(provenance.content_worktree_hashes(self.work),frozen)
        path.write_bytes(b'second')
        self.assertNotEqual(provenance.content_worktree_hashes(self.work),frozen)


if __name__ == '__main__':
    unittest.main()
