"""Deterministic gzip assets, inflated by the browser rather than device RAM."""
from pathlib import Path
import argparse,gzip

ASSETS={'index.html':'vibe_index','companion.css':'vibe_css','companion.js':'vibe_js',
        'wifi.html':'vibe_wifi','wifi.js':'vibe_wifi_js','network.js':'vibe_network_js',
        'vendor/gifuct.js':'vibe_gif','alpha-codec.js':'vibe_alpha'}
def compress(source,destination):
    destination.mkdir(parents=True,exist_ok=True)
    for name,symbol in ASSETS.items():
        data=(source/name).read_bytes()
        packed=gzip.compress(data,compresslevel=9,mtime=0)
        assert gzip.decompress(packed)==data
        (destination/(symbol+'.gz')).write_bytes(packed)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    args=p.parse_args();compress(args.source,args.destination)
