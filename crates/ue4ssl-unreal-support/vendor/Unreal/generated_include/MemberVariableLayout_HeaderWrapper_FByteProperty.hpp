static std::unordered_map<RC::StringType, int32_t> MemberOffsets;
static std::unordered_map<RC::StringType, BitfieldInfo> BitfieldInfos;

public:
    TObjectPtr<UEnum>& GetEnum();
    const TObjectPtr<UEnum>& GetEnum() const;

public:
    static int32_t& UEP_TotalSize();
