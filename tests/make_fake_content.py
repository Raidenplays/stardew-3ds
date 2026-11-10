import os, json, numpy as np
from PIL import Image
C="fake/Content (unpacked)"
def png(p,w,h):
    os.makedirs(os.path.dirname(f"{C}/{p}.png"),exist_ok=True)
    a=np.random.randint(0,255,(h,w,4),dtype=np.uint8); Image.fromarray(a).save(f"{C}/{p}.png")
for s in ["spring","summer","fall","winter"]: png(f"Maps/{s}_outdoorsTileSheet",400,1264)
png("Maps/paths",64,64)
for p,w,h in [("Maps/springobjects",384,624),("TileSheets/crops",256,800),("TileSheets/tools",336,384),("TerrainFeatures/hoeDirt",128,64),("Characters/Farmer/farmer_base",288,672),("Characters/Farmer/hairstyles",128,672),("Buildings/houses",272,432)]: png(p,w,h)
W,H=6,4
back=",".join(str(1+ (i%5)) for i in range(W*H)); bld=",".join("0" if i!=7 else "3" for i in range(W*H))
tmx=f'''<?xml version="1.0"?><map version="1.0" orientation="orthogonal" width="{W}" height="{H}" tilewidth="16" tileheight="16">
<tileset firstgid="1" name="untitled tile sheet" tilewidth="16" tileheight="16" tilecount="1975" columns="25"><image source="spring_outdoorsTileSheet.png" width="400" height="1264"/>
<tile id="0"><properties><property name="Diggable" value="T"/></properties></tile>
<tile id="1"><properties><property name="Water" value="T"/></properties><animation><frame tileid="1" duration="250"/><frame tileid="2" duration="250"/></animation></tile>
</tileset>
<tileset firstgid="1976" name="Paths" tilewidth="16" tileheight="16" tilecount="16" columns="4"><image source="paths.png" width="64" height="64"/></tileset>
<layer name="Back" width="{W}" height="{H}"><data encoding="csv">{back}</data></layer>
<layer name="Buildings" width="{W}" height="{H}"><data encoding="csv">{bld}</data></layer>
<layer name="Paths" width="{W}" height="{H}"><data encoding="csv">{",".join("0" for _ in range(W*H))}</data></layer>
</map>'''
open(f"{C}/Maps/Farm.tmx","w").write(tmx)
os.makedirs(f"{C}/Data",exist_ok=True)
json.dump({"472":{"Seasons":["Spring"],"DaysInPhase":[1,1,1,1],"RegrowDays":-1,"IsRaised":False,"HarvestItemId":"24","Texture":"TileSheets\\crops","SpriteIndex":0},"(O)Bad":{}},open(f"{C}/Data/Crops.json","w"))
json.dump({"24":{"Name":"Parsnip","Price":35,"SpriteIndex":24,"Category":-75,"Edibility":10,"Texture":None},"472":{"Name":"Parsnip Seeds","Price":10,"SpriteIndex":472,"Category":-74}},open(f"{C}/Data/Objects.json","w"))
json.dump({"Hoe":{"ClassName":"Hoe","Name":"Hoe","SpriteIndex":21,"MenuSpriteIndex":-1,"Texture":"TileSheets\\tools","UpgradeLevel":0}},open(f"{C}/Data/Tools.json","w"))
