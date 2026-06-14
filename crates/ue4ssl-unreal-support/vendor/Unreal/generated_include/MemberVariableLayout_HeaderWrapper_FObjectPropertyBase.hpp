static std::unordered_map<RC::StringType, int32_t> MemberOffsets;
static std::unordered_map<RC::StringType, BitfieldInfo> BitfieldInfos;

public:
    TObjectPtr<UClass>& GetPropertyClass();
    const TObjectPtr<UClass>& GetPropertyClass() const;

public:
    static int32_t& UEP_TotalSize();
