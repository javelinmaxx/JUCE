#pragma once
namespace zao
{
struct Settings
{
    float inputDb = 0, driveDb = 12, toneHz = 10000, outputDb = -6, mix = 1;
    float coreDriveDb = 6, dcBias = 0;
    int model = 0;
    bool transformer = false, bypass = false;
    float bjtBias = 0, diodeKnee = 0.5f;
    bool smart = false;
};
}
