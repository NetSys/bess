#ifndef BESS_DEBUG_H_
#define BESS_DEBUG_H_

namespace bess {
namespace debug {

void SetTrapHandler(void);
void GoPanic(void);
void DumpTypes(void);

}  // namespace debug
}  // namespace bess

#endif  // BESS_DEBUG_H_
