#include <stdint.h>
static const size_t TBL24_SIZE = (1u << 24) + 1;
static const uint16_t OVERFLOW_MASK = 0x8000;

struct IPLookupTable {
	std::unordered_map<uint32_t, uint16_t> prefixTable_[33];
	uint16_t tbl24[TBL24_SIZE];
	uint16_t tblLong[TBL24_SIZE];
	uint32_t currentTBLLong;
}
