#include "map/Map.h"
#include <cassert>
#include <iostream>
int main() {
 using namespace MapGenerator;
 Map map(64,32,"generator-smoke.log");
 map.SetLog(false);
 std::vector<uint8_t> mask(64*32*4,255);
 for(unsigned y=0;y<32;++y)for(unsigned x=32;x<64;++x) {
 auto i=(y*64+x)*4; mask[i]=mask[i+1]=mask[i+2]=0;
 }
 LookupMapData spec; spec.width=64; spec.height=32; spec.land.tiles=8;spec.ocean.tiles=8;spec.land.seed=42;spec.ocean.seed=43;spec.borderLine=1;
 map.GenerateMap(mask,64,32,spec,MapModeGen::FromHeightMap);
 const auto& tiles=map.GetLookupTileMap();
 assert(tiles.GetTiles().size()==64*32);
 assert(tiles.ConvertTileMapToBuffer().size()==64*32*4);
 assert(tiles.GetColorsInUse()>1);
 TileMap invalid(2,2);
 assert(invalid.GetMapOfPointsWhereColorDifferentThanCentroid(data::Color(255,0,0,255),data::Color(0,0,0,255)).size()==16);
 std::cout<<"PASS: binary-heightmap province generation, BGRA buffer, invalid-centroid query; colors="<<tiles.GetColorsInUse()<<"\n";
}
