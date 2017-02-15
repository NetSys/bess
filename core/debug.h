#ifndef BESS_DEBUG_H_
#define BESS_DEBUG_H_

#include <string>

namespace bess {
namespace debug {

void SetTrapHandler(void);
[[noreturn]] void GoPanic(void);
void DumpTypes(void);
std::string DumpStack();

}  // namespace debug
}  // namespace bess

#endif  // BESS_DEBUG_H_
