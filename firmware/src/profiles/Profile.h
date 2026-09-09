#pragma once

class ProfileLifecycle {
public:
    virtual ~ProfileLifecycle() = default;
    virtual bool enter() = 0;
    virtual void exit() = 0;
};
