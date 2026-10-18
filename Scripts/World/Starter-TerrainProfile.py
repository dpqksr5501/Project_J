"""Read preserved height samples through a RegionManifest, without Unreal imports."""
import array,hashlib,json,math,sys
from pathlib import Path
class TerrainProfile:
    def __init__(self,manifest_path):
        manifest_path=Path(manifest_path);self.manifest=json.loads(manifest_path.read_text(encoding='utf-8'));self.grid=self.manifest['world_grid'];terrain=self.manifest['terrain_profile']
        raw=(manifest_path.parent/terrain['source']).read_bytes();assert hashlib.sha256(raw).hexdigest()==terrain['sha256'],'Unrecognized source height change'
        self.samples=array.array('H');self.samples.frombytes(raw);self.width,self.height_count=self.grid['height_resolution'];assert len(self.samples)==self.width*self.height_count
        if sys.byteorder!='little':self.samples.byteswap()
        self.spacing=self.grid['height_sample_spacing_m'];self.z_metres=self.grid['landscape_z_scale']/128/100
    def height(self,x,y):
        assert 0<=x<=(self.width-1)*self.spacing and 0<=y<=(self.height_count-1)*self.spacing
        u,v=x/self.spacing,y/self.spacing;i,j=math.floor(u),math.floor(v);s,t=u-i,v-j;ii,jj=min(i+1,self.width-1),min(j+1,self.height_count-1);a=self.samples;n=self.width
        return ((a[j*n+i]*(1-s)+a[j*n+ii]*s)*(1-t)+(a[jj*n+i]*(1-s)+a[jj*n+ii]*s)*t-32768)*self.z_metres
