// The normal portable Gold initializer supplies an explicitly owned independent
// local service. Preserve the native async account/session constructors and
// results; no vendor identity, account cache or synthetic success is supplied.
bool IndependentSessionPrepare(std::vector<Patch>& patches,const CrossplayProduction::Launch& launch){
    if(!launch.mixed || (!launch.host && !launch.IsRoom()) || launch.channel.edition!=Crossplay::Edition::Gold
        || launch.gold_auth_port<1024 || launch.gold_config_port<1024
        || local_login_dispatched || local_login_backend || local_login_context)return false;
    const struct{uintptr_t rva;std::vector<unsigned char> bytes;} guards[]={
        {0x791ff0,{0x8b,0x44,0x24,0x0c,0x8b,0x4c,0x24,0x08}},
        {0x82b780,{0x56,0x8b,0xf1,0xe8,0x98,0x2a,0xf8,0xff}},
        {0x794120,{0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x50}},
        {0x794180,{0x8b,0x01,0x85,0xc0,0x74,0x14}},
        {0x847e50,{0x53,0x55,0x56,0x57,0x8b,0xf1,0xe8,0xf5,0xea,0xf4,0xff}}};
    for(const auto& guard:guards)if(!ValidRange(reinterpret_cast<void*>(image+guard.rva),guard.bytes.size())
        || memcmp(reinterpret_cast<void*>(image+guard.rva),guard.bytes.data(),guard.bytes.size()))return false;
    std::string password;
    if(!CrossplayProduction::GoldLocalSecret(launch,password) || password.size()!=64
        || strcpy_s(local_login_name,launch.name.c_str()) || strcpy_s(local_login_secret,password.c_str()))return false;
    SecureZeroMemory(&password[0],password.size());
    local_login_port=launch.gold_auth_port;local_multiplayer_enabled=true;
    if(!AccountLoginPrepare(patches,launch.gold_config_port))return false;
    return Add(patches,0x655cfb,{0xe8,0x80,0x5e,0x1d,0x00},LocalLoginAccessKey);
}
