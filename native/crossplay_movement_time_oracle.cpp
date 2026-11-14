#include "crossplay_movement_time_math.h"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    FILE *input=nullptr,*output=nullptr;
    if(fopen_s(&input,argv[1],"rb") || fopen_s(&output,argv[2],"wb"))return 2;
    float pair[2]{};
    for(;;) {
        const auto bytes=fread(pair,1,sizeof(pair),input);
        if(!bytes)break;
        if(bytes!=sizeof(pair))return 3;
        const int value=MovementTimeMath::Milliseconds(pair[0],pair[1]);
        if(fwrite(&value,sizeof(value),1,output)!=1)return 4;
    }
    const bool okay=!ferror(input) && fclose(input)==0 && fclose(output)==0;
    return okay?0:5;
}
