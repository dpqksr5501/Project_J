"""Build a portable catalog, review viewer and ranked (NOT approved) connections.

Usage: python Build-AnimationSurvey.py CLIP_JSON_DIRECTORY OUTPUT_DIRECTORY
Requires only Python's standard library; consumes Survey-AnimationFBX.py output.
"""
from pathlib import Path
from collections import Counter, defaultdict
import json, csv, sys, re, math, hashlib

CORE = ['Bip01 Pelvis', 'Bip01 Spine2', 'Bip01 Head']
for side in ('L', 'R'):
    CORE += [f'Bip01 {side} {part}' for part in ('UpperArm', 'Forearm', 'Hand', 'Thigh', 'Calf', 'Foot')]


def classify(name):
    if name.startswith('facial_'):
        return '얼굴'
    if 'customizing' in name or 'figure' in name:
        return '커스터마이징'
    if 'riding' in name:
        return '탑승'
    if 'return' in name:
        return '복귀'
    if 'weapon_in' in name or 'weapon_out' in name:
        return '장착·수납'
    if '_att_' in name or '_skill_att_' in name:
        return '공격·스킬'
    if '_def_' in name:
        return '방어'
    if '_move_' in name:
        return '이동'
    if 'idle' in name:
        return '대기'
    return '기타'


def family(name):
    m = re.search(r'pdw_(\d\d_\d\d)_', name)
    return m[1] if m else 'unknown'


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


def features(window):
    rows = []
    for sample in window:
        bones = sample['bones']
        if not all(n in bones for n in CORE):
            return []
        root = bones['Bip01 Pelvis']['p']
        # Remove horizontal travel only. Keep height/facing/posture intact.
        pos = {n: [bones[n]['p'][0]-root[0], bones[n]['p'][1]-root[1], bones[n]['p'][2]] for n in CORE}
        rows.append({'t': sample['t'], 'p': pos, 'q': {n: bones[n]['q'] for n in CORE}, 'v': {}})
    for i, row in enumerate(rows):
        before, after = rows[max(0,i-1)], rows[min(len(rows)-1,i+1)]
        dt = after['t']-before['t']
        if dt <= 0:
            return []
        row['v'] = {n: [(y-x)/dt for x,y in zip(before['p'][n],after['p'][n])] for n in CORE}
    return rows


def match(a, b):
    best = None
    for x in a:
        for y in b:
            pos = sum(distance(x['p'][n],y['p'][n]) for n in CORE)/len(CORE)*100
            angle = sum(math.degrees(2*math.acos(min(1,abs(sum(v*w for v,w in zip(x['q'][n],y['q'][n])))))) for n in CORE)/len(CORE)
            vel = sum(distance(x['v'][n],y['v'][n]) for n in CORE)/len(CORE)
            score = pos + .15*angle + 5*vel
            if best is None or score < best['score']:
                best = {'score':round(score,3), 'position_cm':round(pos,3),
                        'rotation_deg':round(angle,3), 'velocity_m_s':round(vel,3),
                        'from_s':x['t'], 'to_s':y['t']}
    return best


def write_csv(path, rows, columns):
    with path.open('w', encoding='utf-8-sig', newline='') as f:
        w=csv.DictWriter(f, fieldnames=columns, extrasaction='ignore')
        w.writeheader(); w.writerows(rows)


def contact_sheets(rows, output):
    """Optional static review pages; viewer/catalog do not require Pillow."""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 17)
    small = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 14)
    edges = [('Pelvis','Spine'),('Spine','Spine1'),('Spine1','Spine2'),('Spine2','Neck'),('Neck','Head')]
    for side in ('L','R'):
        edges += [('Spine2',side+' Clavicle'),(side+' Clavicle',side+' UpperArm'),
                  (side+' UpperArm',side+' Forearm'),(side+' Forearm',side+' Hand'),
                  ('Pelvis',side+' Thigh'),(side+' Thigh',side+' Calf'),
                  (side+' Calf',side+' Foot'),(side+' Foot',side+' Toe0')]
    selected=[r for r in rows if r['category'] in ('공격·스킬','복귀','장착·수납') and r['family'] in ('01_01','05_00','05_01')]
    for page in range(math.ceil(len(selected)/10)):
        subset=selected[page*10:(page+1)*10]
        im=Image.new('RGB',(1600,70+len(subset)*205),'#101923');d=ImageDraw.Draw(im)
        d.text((16,15),f'FBX survey / skeletal poses / page {page+1} / five normalized times (not gameplay approval)',font=font,fill='#e9f3fa')
        for i,r in enumerate(subset):
            y=60+i*205
            d.text((16,y),r['name']+f"  [{r['duration_s']:.3f}s]",font=font,fill='#c6dded')
            zs=[b['p'][2] for s in r['preview'] for n,b in s['bones'].items() if n.startswith('Bip01 ')]
            scale=min(65,135/(max(zs)-min(zs)+.3))
            baseline=y+173+min(0,min(zs))*scale
            for col,index in enumerate((0,4,8,12,16)):
                b=r['preview'][index]['bones'];root=b.get('Bip01 Pelvis',{}).get('p',[0,0,0])
                def project(p):
                    x,yz,z=p
                    return (160+col*315+((x-root[0])*.825+(yz-root[1])*.565)*scale,baseline-z*scale)
                d.line((col*315+20,y+173,col*315+300,y+173),fill='#394b5c')
                for a,c in edges:
                    aa,cc='Bip01 '+a,'Bip01 '+c
                    if aa in b and cc in b:
                        d.line((project(b[aa]['p']),project(b[cc]['p'])),fill='#79caff' if 'L ' in a else '#e3ca91',width=3)
                for a,c in [('Bip01 R Hand','Bip_Weapon_R'),('Bip01 L Hand','Bip_Weapon_L')]:
                    if a in b and c in b:d.line((project(b[a]['p']),project(b[c]['p'])),fill='#fa7979',width=3)
                if 'Bip01 Head' in b:
                    x,z=project(b['Bip01 Head']['p']);d.ellipse((x-5,z-5,x+5,z+5),outline='#e5eff7',width=2)
                d.text((col*315+130,y+179),f"{r['duration_s']*index/16:.2f}s",font=small,fill='#a4bacf')
        im.save(output/f'Contact-{page+1:02}.png')
    return len(selected),math.ceil(len(selected)/10)


def main(raw, output):
    raw, output = Path(raw), Path(output)
    receipt = json.loads((raw/'Receipt.json').read_text('utf-8'))
    if receipt['failures'] or receipt['completed'] != receipt['files']:
        raise ValueError('Survey is incomplete; do not publish a partial catalog as complete')
    rows = [json.loads(p.read_text('utf-8')) for p in sorted(raw.glob('pdw*.json'))]
    rows += [json.loads(p.read_text('utf-8')) for p in sorted(raw.glob('facial*.json'))]
    if len(rows) != receipt['files']:
        raise ValueError('Clip count does not match receipt')
    output.mkdir(parents=True, exist_ok=True)
    by_motion, by_file = defaultdict(list), defaultdict(list)
    for row in rows:
        row.setdefault('static_pose',False)
        row['family'] = family(row['name']); row['category'] = classify(row['name'])
        row['duration_s'] = round(row['duration_s'],6)
        row['source_key_hz'] = row['metadata']['key_rate_hz']
        row['phase'] = next((p for p in ('start','ing','end','return') if re.search(r'_'+p+r'(?:_|$)',row['name'])), '')
        pelvis = [s['bones'].get('Bip01 Pelvis',{}).get('p') for s in row['preview']]
        pelvis = [p for p in pelvis if p]
        row['pelvis_net_cm'] = round(distance(pelvis[0],pelvis[-1])*100,3) if pelvis else None
        row['pelvis_path_sampled_cm'] = round(sum(distance(a,b) for a,b in zip(pelvis,pelvis[1:]))*100,3) if pelvis else None
        by_motion[(row['rest_signature'],row['motion_sha256'])].append(row['name'])
        by_file[row['source_sha256']].append(row['name'])
    connections=[]
    starts={r['name']:features(r['start_window']) for r in rows}
    ends={r['name']:features(r['end_window']) for r in rows}
    interiors={r['name']:[f for f in features(r['preview']) if .05*r['duration_s'] < f['t'] < .95*r['duration_s']] for r in rows}
    for source in rows:
        if source['category'] != '공격·스킬':
            continue
        # Recovery ranking deliberately stays within the named source family.
        targets=[r for r in rows if r['family']==source['family'] and r['category']=='복귀'
                 and r['rest_signature']==source['rest_signature']]
        ranked=[]
        for target in targets:
            if ends[source['name']] and starts[target['name']]:
                score=match(ends[source['name']],starts[target['name']])
                ranked.append(dict(source=source['name'],target=target['name'],kind='끝 구간 복귀 후보',**score))
        ranked.sort(key=lambda r:r['score'])
        for rank,r in enumerate(ranked[:3],1):
            r['rank']=rank; connections.append(r)
        ranked=[]
        for target in targets:
            if interiors[source['name']] and starts[target['name']]:
                score=match(interiors[source['name']],starts[target['name']])
                ranked.append(dict(source=source['name'],target=target['name'],kind='내부 복귀 후보 · 희소 샘플',**score))
        ranked.sort(key=lambda r:r['score'])
        for rank,r in enumerate(ranked[:3],1):
            r['rank']=rank; connections.append(r)
    # Numbered attacks and authored start/ing/end groups: names only nominate
    # candidates; the score is independent evidence and never defines gameplay.
    groups=defaultdict(list)
    for r in rows:
        key=re.sub(r'_(?:start|ing|end|return)(?=_|$)','',r['name'])
        key=re.sub(r'_\d+(?:_\d+)*$','',key)
        if r['category'] in ('공격·스킬','장착·수납'):
            groups[key].append(r)
    for key, members in groups.items():
        if len(members)<2 or len(members)>12:
            continue
        for a in members:
            ranked=[]
            for b in members:
                if a is b or a['rest_signature']!=b['rest_signature']:
                    continue
                if ends[a['name']] and starts[b['name']]:
                    score=match(ends[a['name']],starts[b['name']])
                    ranked.append(dict(source=a['name'],target=b['name'],kind='동일 계열 연결 후보',**score))
            ranked.sort(key=lambda r:r['score'])
            for rank,r in enumerate(ranked[:3],1):
                r['rank']=rank; connections.append(r)
            ranked=[]
            for b in members:
                if a is b or a['rest_signature']!=b['rest_signature']:
                    continue
                if interiors[a['name']] and starts[b['name']]:
                    score=match(interiors[a['name']],starts[b['name']])
                    ranked.append(dict(source=a['name'],target=b['name'],kind='내부 연타 후보 · 희소 샘플',**score))
            ranked.sort(key=lambda r:r['score'])
            for rank,r in enumerate(ranked[:3],1):
                r['rank']=rank; connections.append(r)
    refined_path=raw.parent/'RefinedConnections.json'
    refinement=None
    if refined_path.exists():
        refinement=json.loads(refined_path.read_text('utf-8'))
        if refinement['status']!='complete':raise ValueError('Refinement still running')
        hashes={r['name']:r['source_sha256'] for r in rows}
        for pair in refinement['rows']:
            for inp in pair['inputs']:
                if hashes.get(inp['name'])!=inp['sha256']:raise ValueError('Stale refinement')
            connections.append(pair)
        (output/'RefinedConnections.json').write_text(json.dumps(refinement,ensure_ascii=False,indent=2),'utf-8')
    columns=['name','family','category','phase','duration_s','static_pose','source_key_hz','analysis_fps','armature_bones',
             'sample_bones','curves','pelvis_net_cm','pelvis_path_sampled_cm','rest_signature','motion_sha256','source_sha256']
    write_csv(output/'Catalog.csv',rows,columns)
    write_csv(output/'Connections.csv',connections,['source','target','kind','rank','score','position_cm','rotation_deg','velocity_m_s','from_s','to_s'])
    summary={'schema':1,'files':len(rows),'family_counts':dict(Counter(r['family'] for r in rows)),
             'category_counts':dict(Counter(r['category'] for r in rows)),
             'key_rates_hz':dict(Counter(str(round(r['source_key_hz'],4)) if r['source_key_hz'] else 'none' for r in rows)),
             'bone_counts':dict(Counter(r['armature_bones'] for r in rows)),
             'rest_signatures':len(set(r['rest_signature'] for r in rows)),
             'exact_file_duplicate_groups':[v for v in by_file.values() if len(v)>1],
             'converted_motion_duplicate_groups':[v for v in by_motion.values() if len(v)>1],
             'ranked_connections':len(connections),'refined_pairs':len(refinement['rows']) if refinement else 0,'receipt':receipt,
             'method':'15 body joints; pelvis XY removed; facing/height retained; endpoint windows <=0.30s at 10 samples; separate interior candidates from 17 sparse samples; score=cm + 0.15*deg + 5*m/s; same rest signature only',
             'limitations':['No original gameplay combo/cancel/notify semantics in FBX.',
                            'Interior candidates use coarse 17-sample positions/velocity; not approved cut times.',
                            'Body score excludes fingers, weapon, cloth and face; visually check weapon tracks.',
                            'Viewer uses 17 evaluated samples, linear position interpolation, not source-rate playback.',
                            'Pelvis displacement is not UE extracted root motion. No runtime assets changed.']}
    (output/'Summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),'utf-8')
    lines=['# Greatsword_A 전투·장착·복귀 파일 묶음', '',
           '파일명으로 묶은 목록이다. 번호 순서·start/ing/end 표기는 제작 의도 후보이며 실제 게임의 입력·연타 계약을 확정하지 않는다.',
           '길이는 FBX Take와 Blender 평가를 비교한 초 단위 값이다. 전체 401개는 [Catalog.csv](Catalog.csv), 포즈는 [Review.html](Review.html)을 본다.', '']
    for fam in ('01_01','05_00','05_01'):
        gs=defaultdict(list)
        for r in rows:
            if r['family']==fam and r['category'] in ('공격·스킬','장착·수납','복귀'):
                short=r['name'].removeprefix('pdw_'+fam+'_')
                key=re.sub(r'_(?:start|ing|end|return)(?=_|$)','',short)
                key=re.sub(r'_\d+(?:_\d+)*$','',key)
                gs[key].append((short,r['duration_s']))
        lines += ['## '+fam, '', '| 파일명 묶음 | 실제 파일명 뒤쪽 · 길이 |','| --- | --- |']
        for key,members in sorted(gs.items()):
            lines.append('| `'+key+'` | '+'<br>'.join('`'+n+'` '+f'{d:.3f}s' for n,d in members)+' |')
        lines.append('')
    (output/'Groups.md').write_text('\n'.join(lines),'utf-8')
    compact=[]
    for r in rows:
        compact.append({k:r[k] for k in ('name','family','category','duration_s','phase','pelvis_net_cm')} |
                       {'samples':[{n:b['p'] for n,b in s['bones'].items()} for s in r['preview']]})
    data=json.dumps({'clips':compact,'connections':connections},ensure_ascii=False,separators=(',',':')).replace('</','<\\/')
    template=Path(__file__).with_name('AnimationSurveyViewer.html').read_text('utf-8')
    (output/'Review.html').write_text(template.replace('/*SURVEY_DATA*/',data),'utf-8')
    try:
        count,pages=contact_sheets(rows,output)
        print(f'Contact sheets: {count} clips / {pages} PNG pages')
    except ImportError:
        print('Pillow unavailable; HTML viewer remains complete.')
    print(json.dumps({k:v for k,v in summary.items() if k!='receipt'},ensure_ascii=False,indent=2))


if __name__=='__main__':
    main(*sys.argv[1:])
