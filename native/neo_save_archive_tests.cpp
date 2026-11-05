#include "neo_save_archive.h"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        bool corrected=false;const auto state=neo_save::Read(input,std::string(argv[2])=="1",&corrected);
        std::cout<<state;return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
