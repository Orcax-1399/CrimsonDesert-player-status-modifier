#include "scanner.h"

#include "logger.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct PatternDefinition {
    const char* name = nullptr;
    const char* text = nullptr;
    std::size_t hook_offset = 0;
};

struct SectionSpan {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
    std::string section_name;
    bool executable = false;
};

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct MatchResult {
    uintptr_t address = 0;
    std::string section_name;
};

enum class ScanStatus {
    NotFound,
    Unique,
    Ambiguous,
};

struct ScanOutcome {
    uintptr_t address = 0;
    ScanStatus status = ScanStatus::NotFound;
};

SectionSpan EnumerateMainModuleImageSpan() {
    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
        return {};
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return {};
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return {};
    }

    const auto image_size = static_cast<std::size_t>(nt_headers->OptionalHeader.SizeOfImage);
    if (image_size == 0) {
        Log("scanner: main-module image size is zero");
        return {};
    }

    return {
        module,
        image_size,
        "[image]",
        true,
    };
}

std::vector<SectionSpan> EnumerateMainModuleSections(const bool text_only) {
    std::vector<SectionSpan> sections;

    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
        return sections;
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return sections;
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return sections;
    }

    const auto first_section = IMAGE_FIRST_SECTION(nt_headers);
    for (unsigned i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
        const auto& section = first_section[i];

        char name_buffer[9]{};
        std::memcpy(name_buffer, section.Name, 8);
        const std::string section_name(name_buffer);

        if (text_only && section_name != ".text") {
            continue;
        }

        if (section.Misc.VirtualSize == 0) {
            continue;
        }

        sections.push_back({
            module + section.VirtualAddress,
            static_cast<std::size_t>(section.Misc.VirtualSize),
            section_name,
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0,
        });
    }

    return sections;
}

std::vector<PatternByte> ParsePattern(const char* text) {
    std::vector<PatternByte> bytes;
    std::string token;

    for (const char* cursor = text; *cursor != '\0';) {
        while (*cursor == ' ') {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        const char* token_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }

        token.assign(token_start, cursor);
        if (token == "?" || token == "??" || token == "*") {
            bytes.push_back({0, true});
        } else {
            bytes.push_back({static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }

    return bytes;
}

std::vector<MatchResult> ScanPattern(const std::vector<SectionSpan>& spans,
                                     const std::vector<PatternByte>& pattern,
                                     const std::size_t max_results) {
    std::vector<MatchResult> results;
    if (pattern.empty()) {
        return results;
    }

    for (const auto& span : spans) {
        if (span.begin == nullptr || span.size < pattern.size()) {
            continue;
        }

        const auto end = span.size - pattern.size();
        for (std::size_t offset = 0; offset <= end; ++offset) {
            bool matched = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (!pattern[index].wildcard && span.begin[offset + index] != pattern[index].value) {
                    matched = false;
                    break;
                }
            }

            if (!matched) {
                continue;
            }

            results.push_back({
                reinterpret_cast<uintptr_t>(span.begin + offset),
                span.section_name,
            });

            if (results.size() >= max_results) {
                return results;
            }
        }
    }

    return results;
}

ScanOutcome ScanInSections(const PatternDefinition* definitions,
                          const std::size_t definition_count) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool text_only = pass == 0;
        const auto sections = EnumerateMainModuleSections(text_only);
        if (sections.empty()) {
            continue;
        }

        for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
            const auto& definition = definitions[definition_index];
            const auto pattern = ParsePattern(definition.text);
            const auto matches = ScanPattern(sections, pattern, 16);

            if (matches.size() == 1) {
                return {
                    matches.front().address + definition.hook_offset,
                    ScanStatus::Unique,
                };
            }

            if (matches.size() > 1) {
                return {
                    0,
                    ScanStatus::Ambiguous,
                };
            }
        }
    }

    const auto image_span = EnumerateMainModuleImageSpan();
    if (image_span.begin == nullptr || image_span.size == 0) {
        return {};
    }

    const std::vector<SectionSpan> image_spans{image_span};
    for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
        const auto& definition = definitions[definition_index];
        const auto pattern = ParsePattern(definition.text);
        const auto matches = ScanPattern(image_spans, pattern, 16);

        if (matches.size() == 1) {
            return {
                matches.front().address + definition.hook_offset,
                ScanStatus::Unique,
            };
        }

        if (matches.size() > 1) {
            return {
                0,
                ScanStatus::Ambiguous,
            };
        }
    }

    return {};
}

}  // namespace

PlayerPointerCaptureTarget ScanForPlayerPointerCapture() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "49 8B 7D 18 49 8B 44 24 40 48 8B 40 68 48 8B 70 20",
            17,
        },
        {
            "fallback",
            "49 8B 44 24 40 48 8B 40 68 48 8B 70 20",
            13,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: player-pointer found multiple matches, install failed");
        return {};
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: player-pointer found 0 matches");
        return {};
    }

    return {outcome.address};
}

MountPointerCaptureTarget ScanForMountPointerCapture() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 8B C7 49 8B 7D 08 80 BF 94 00 00 00 00 0F 85 ?? ?? ?? ?? 48 8B 47 68 48 8B 48 20 48 83 C1 30 E8 ?? ?? ?? ?? 66 83 B8 E4 00 00 00 00",
            20,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: mount-pointer found multiple matches, install failed");
        return {};
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: mount-pointer found 0 matches");
        return {};
    }

    return {outcome.address};
}

uintptr_t ScanForPositionHeightAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "49 3B F7 0F 8C ?? ?? ?? ?? 0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00",
            22,
        },
        {
            "fallback",
            "0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00",
            13,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: position-height found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: position-height found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDamageBattleAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9",
            0,
        },
        {
            "fallback",
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9 0F 84 ?? ?? ?? ??",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: damage-battle found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: damage-battle found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDragonVillageSummonJump() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "84 C0 74 09 41 89 1C 24 E9 ?? ?? ?? ?? 8B 47 0C",
            2,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: dragon-village-summon found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: dragon-village-summon found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDragonFlyingRestrictWrite() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "41 88 47 04 41 89 3F 48 8B 5C 24 ??",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: dragon-flying-restrict found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: dragon-flying-restrict found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDragonRoofRestrictTest() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "85 DB 74 59 48 8B 05 ?? ?? ?? ?? 48 8B 48 20 8B D3 48 8B 89 A0 07 00 00",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: dragon-roof-restrict found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: dragon-roof-restrict found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForItemGainAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "49 01 4C 38 10", 0},
    };

    // Known item-loss opcode kept for future work:
    //   49 29 4C 07 10    sub [r15+rax+10], rcx
    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: item-gain found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: item-gain found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAffinityGainPrepare() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B6 47 38 88 45 ? 0F B6 47 39 88 45 ? 8B 47 08 89 45 ? 48 8B 47 10 48 89 45 ? 0F 10 47 18",
            20,
        },
        {
            "fallback",
            "8B 47 08 89 45 ? 48 8B 47 10 48 89 45 ? 0F 10 47 18 0F 11 45 ?",
            6,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: affinity-gain-prepare found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: affinity-gain-prepare found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAffinityCurrentWrite() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 89 43 48 41 8B CD 83 E9 01 74 0A 83 F9 01 75 09 88 4B 3F",
            0,
        },
        {
            "fallback",
            "C7 43 40 00 00 00 00 48 89 43 48 41 8B CD 83 E9 01 74 0A",
            7,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: affinity-current-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: affinity-current-write found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDurabilityWriteAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "66 3B CF 66 0F 4C F9 66 89 7B 50 48 8B 5C 24 20 48 8B 03 33 D2 48 8B CB FF 50 20",
            7,
        },
        {
            "fallback",
            "66 89 7B 50 48 8B 5C 24 20 48 8B 03 33 D2 48 8B CB FF 50 20",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: durability-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: durability-write found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDurabilityDeltaAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B7 C7 66 41 03 C5 66 89 45 38 79 0B 33 C0 66 89 45 38 0F B7 C8",
            3,
        },
        {
            "fallback",
            "66 41 03 C5 66 89 45 38 79 0B 33 C0 66 89 45 38",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: durability-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: durability-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAbyssDurabilityDeltaAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B7 73 02 48 8B CB 66 41 3B F5 42 8D 04 2E 66 0F 4D F8 66 89 7B 02 E8",
            11,
        },
        {
            "fallback",
            "66 41 3B F5 42 8D 04 2E 66 0F 4D F8 66 89 7B 02",
            4,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: abyss-durability-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: abyss-durability-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStaminaAb00Access() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B7 D7 49 8B CE E8 ?? ?? ?? ?? 48 8B F0 48 85 DB 74 ?? 33 C0 66 89 44 24 20 38 46 53",
            11,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stamina-ab00 found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stamina-ab00 found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForSpiritDeltaAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 89 ?? 48 89 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 ?? 48 8B 74 24 ?? 48 83 C4 ?? 5F C3",
            6,
        },
        {
            "fallback",
            "49 89 D8 48 89 FA 48 89 C1 48 89 C6 E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 30 48 8B 74 24 40 48 83 C4 20 5F C3",
            12,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: spirit-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: spirit-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStatsAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24", 12},
        {"fallback", "48 C1 E0 04 48 03 46 58", 8},
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stats found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stats found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStatWriteAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 2B 47 18 48 39 5F 18 48 0F 4F C2 48 89 47 20 48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38 66 89 6F 50",
            20,
        },
        {
            "fallback",
            "48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38",
            4,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stat-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stat-write found 0 matches");
        return 0;
    }

    return outcome.address;
}
