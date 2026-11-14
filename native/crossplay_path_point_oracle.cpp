#include "crossplay_path_point_math.h"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    FILE *input=nullptr,*output=nullptr;
    if(fopen_s(&input,argv[1],"rb") || fopen_s(&output,argv[2],"wb"))return 2;
    struct Input {float values[5];unsigned plus;};
    static_assert(sizeof(Input)==24,"Oracle input layout");
    Input row{};
    while(fread(&row,sizeof(row),1,input)==1) {
        float point[2]{};
        HistoryPathPointMath::Point(row.values[0],row.values[1],row.values[2],
            row.values[3],row.values[4],row.plus!=0,point);
        if(fwrite(point,sizeof(point),1,output)!=1)return 3;
    }
    fclose(input);fclose(output);return 0;
}
