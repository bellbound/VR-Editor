#pragma once

// Interface for objects that want to receive per-frame update callbacks
class IFrameUpdateListener
{
public:
    virtual ~IFrameUpdateListener() = default;

    // Called every frame with delta time in seconds
    virtual void OnFrameUpdate(float deltaTime) = 0;
};
