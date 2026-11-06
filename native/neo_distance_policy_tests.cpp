#include "neo_distance_policy.h"
#include <cstdio>
#include <limits>
int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool value){++checks;if(!value)++failures;};
    using neo_distance::Route;using neo_distance::Before;
    const Route short_first{2,98,65536},short_total{20,30,65537};
    check(Before(short_total,short_first));check(!Before(short_first,short_total));
    check(Before(short_total,{}));check(!Before({},short_total));
    check(!Before(short_total,short_total));
    check(Before({30,20,65536},short_total));check(!Before({30,20,65538},short_total));
    const float invalid_values[]={-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
    for(float invalid:invalid_values) {
        check(!Before({invalid,1,65536},short_total));check(!Before({1,invalid,65536},short_total));
    }
    check(!Before({0,0,0},short_total));check(Before({0,0,1},short_total));
    // Exhaustively check the ordering's asymmetry/transitivity over finite routes.
    for(unsigned a=0;a<12;++a)for(unsigned b=0;b<12;++b)for(unsigned c=0;c<12;++c) {
        const Route x{float(a%4),float(a/4),a+1},y{float(b%4),float(b/4),b+1},z{float(c%4),float(c/4),c+1};
        check(!(Before(x,y)&&Before(y,x)));
        check(!(Before(x,y)&&Before(y,z))||Before(x,z));
    }
    std::printf("%s %u distance policy checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
