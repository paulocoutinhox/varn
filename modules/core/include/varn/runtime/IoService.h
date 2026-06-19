#pragma once

namespace varn::runtime {

class IoService {
public:
    virtual ~IoService() = default;

    virtual void stop() = 0;
};

} // namespace varn::runtime
