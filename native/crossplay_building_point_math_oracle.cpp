#include "crossplay_building_point_math.h"

#include <cstdio>
#include <float.h>
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    unsigned previous=0;if(_controlfp_s(&previous,_PC_53|_RC_NEAR,_MCW_PC|_MCW_RC))return 5;_mm_setcsr(0x1f80);
    FILE* input=nullptr;FILE* output=nullptr;
    if(fopen_s(&input,argv[1],"rb")||fopen_s(&output,argv[2],"wb"))return 2;
    float v[21]{};unsigned count=0;
    while(fread(v,sizeof(v),1,input)==1) {
        using namespace BuildingPointMath;const Point position{v[0],v[1]};const auto rotation=RotateDegrees(v[2]);float result[18]{};
        for(unsigned i=0;i<2;++i) {
            const unsigned at=3+i*5;const Point p=Transform({v[at],v[at+1]},position,rotation),d=Transform({v[at+2],v[at+3]},position,rotation);
            result[i*5]=p.x;result[i*5+1]=p.y;result[i*5+2]=d.x;result[i*5+3]=d.y;result[i*5+4]=v[at+4];
        }
        const Point act=Transform({v[13],v[14]},position,rotation),lounge=ZeroBits({v[15],v[16]})?Point{}:Transform({v[15],v[16]},position,rotation);
        const Point depot=Transform({v[17],v[18]},position,rotation),constructor=ZeroBits({v[19],v[20]})?act:Transform({v[19],v[20]},position,rotation);
        const Point singles[]={lounge,depot,act,constructor};memcpy(result+10,singles,sizeof(singles));
        if(fwrite(result,sizeof(result),1,output)!=1)return 3;++count;
    }
    fclose(input);fclose(output);printf("Compiled SSE point kernel: %u original-History oracle cases\n",count);return count==2048?0:4;
}
