static std::unordered_map<RC::StringType, int32_t> MemberOffsets;
static std::unordered_map<RC::StringType, BitfieldInfo> BitfieldInfos;

public:
    TObjectPtr<UField>& GetNext();
    const TObjectPtr<UField>& GetNext() const;

public:
    static int32_t& UEP_TotalSize();
