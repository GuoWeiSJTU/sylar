#include "mutex.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count)
    :m_semaphore(static_cast<std::ptrdiff_t>(count)) {
}

Semaphore::~Semaphore() = default;

void Semaphore::wait() {
    m_semaphore.acquire();
}

void Semaphore::notify() {
    m_semaphore.release();
}

}
