"""Refine an explicit review plan near coarse candidates at source key rate.

Blender script; same dedicated scratch-scene requirement as Survey-AnimationFBX.
No gameplay cut/notify is authored. Plan rows: source,target,from_s,to_s.
"""
import bpy, importlib.util, json, math, hashlib
from pathlib import Path


def module(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m


def refine(source, raw, plan, output):
    extract=module('survey_extract','Survey-AnimationFBX.py')
    report=module('survey_report','Build-AnimationSurvey.py')
    source,raw,plan,output=map(Path,(source,raw,plan,output))
    pairs=json.loads(plan.read_text('utf-8'))
    results=[]
    for pair in pairs:
        samples=[]; inputs=[]
        for side,center in (('source',pair['from_s']),('target',pair['to_s'])):
            name=pair[side]
            if Path(name).name!=name:raise ValueError('Unsafe name')
            path=source/(name+'.fbx')
            prior=json.loads((raw/(name+'.json')).read_text('utf-8'))
            before=extract.sha(path)
            if before!=prior['source_sha256']:raise ValueError('Changed source')
            extract.clean();row=extract.extract(path)
            arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
            action=arm.animation_data.action;first=action.frame_range[0]
            fps=bpy.context.scene.render.fps/bpy.context.scene.render.fps_base
            rate=row['metadata']['key_rate_hz']
            if not rate:raise ValueError('No source sample rate')
            lo=max(0,center-.35);hi=min(row['duration_s'],center+.35)
            times=[i/rate for i in range(math.ceil(lo*rate),math.floor(hi*rate)+1)]
            samples.append([extract.sample(arm,bpy.context.scene,t,first,fps) for t in times])
            if before!=extract.sha(path):raise ValueError('Source changed during refinement')
            inputs.append({'name':name,'sha256':before,'rate_hz':rate,'range_s':[lo,hi]})
        best=report.match(*[report.features(s) for s in samples])
        sa=min(samples[0],key=lambda s:abs(s['t']-best['from_s']))
        sb=min(samples[1],key=lambda s:abs(s['t']-best['to_s']))
        weapon={}
        for name in ('Bip_Weapon_R','Bip_Weapon_L'):
            a,b=sa['bones'][name],sb['bones'][name]
            pa,pb=sa['bones']['Bip01 Pelvis']['p'],sb['bones']['Bip01 Pelvis']['p']
            ap=[a['p'][0]-pa[0],a['p'][1]-pa[1],a['p'][2]]
            bp=[b['p'][0]-pb[0],b['p'][1]-pb[1],b['p'][2]]
            dot=min(1,abs(sum(x*y for x,y in zip(a['q'],b['q']))))
            weapon[name]={'position_cm':round(report.distance(ap,bp)*100,4),
                          'rotation_deg':round(math.degrees(2*math.acos(dot)),4)}
        results.append(dict(pair,**{'coarse_from_s':pair['from_s'],'coarse_to_s':pair['to_s']},
                            **best,kind='정밀 재검토 후보 · 원본 키 간격',rank=1,weapon=weapon,inputs=inputs))
        output.write_text(json.dumps({'schema':1,'status':'partial','rows':results},ensure_ascii=False,indent=2),'utf-8')
        yield {'done':len(results),'total':len(pairs),'last':pair['source']}
    extract.clean()
    output.write_text(json.dumps({'schema':1,'status':'complete',
                                  'script_sha256':extract.sha(Path(__file__)),
                                  'plan_sha256':extract.sha(plan),'rows':results},ensure_ascii=False,indent=2),'utf-8')
