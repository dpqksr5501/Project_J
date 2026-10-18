"""Declared saved floors for hunting authoring and verification; no runtime writer."""
import hashlib,importlib.util,json,math
from pathlib import Path
def module(name,file):
    s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
class HuntingSurface:
    def __init__(self,source,terrain,references):
        self.terrain=terrain;self.surfaces=[]
        for ref in references:
            f=Path(source)/ref['source'];assert hashlib.sha256(f.read_bytes()).hexdigest()==ref['sha256'],'Changed authored floor source';p=json.loads(f.read_text());assert ref['owner']==p['owner']
            if ref['kind']=='stair_path':shape=module('support','Starter-SettlementSupport.py').AuthoredPathSupport(p['pieces'])
            else:assert ref['kind']=='mine_gallery';shape=module('support','Starter-SettlementSupport.py').plan_mine_entry_steps(p,terrain.height)
            self.surfaces.append((ref,p,shape))
    def support(self,x,y):
        best=dict(height_m=self.terrain.height(x,y),kind='landscape',actor_path=None)
        for ref,p,shape in self.surfaces:
            if ref['kind']=='stair_path':top=shape.top(x,y)
            else:
                cx,cy,top=p['center_m'];a=math.radians(p['yaw_degrees']);co,si=math.cos(a),math.sin(a);u,v=(x-cx)*co+(y-cy)*si,-(x-cx)*si+(y-cy)*co;w=2.4 if u<8 else 2.4+(u-8)/10*1.2
                if not (-8<=u<=18 and abs(v)<=w):
                    tops=[step['top_m'] for step in shape if abs(u-step['local_u_m'])<=.18+1e-7 and abs(v)<=.9];top=max(tops) if tops else None
            if top is not None and top>best['height_m']:best=dict(height_m=top,kind=ref['kind'],actor_path=ref['actor_path'])
        return best
    def height(self,x,y):return self.support(x,y)['height_m']
    def stance_height(self,x,y,radius):
        # Conservative standing support on the higher adjacent stair tread.
        # Actual CharacterMovement traversal remains a separate required check.
        if self.support(x,y)['kind'] not in ['stair_path','mine_gallery']:return self.height(x,y)
        return max(self.height(x+u,y+v) for u,v in [(0,0),(radius,0),(-radius,0),(0,radius),(0,-radius),(radius*.71,radius*.71),(-radius*.71,radius*.71),(radius*.71,-radius*.71),(-radius*.71,-radius*.71)])
