#pragma once

enum class ScanType
{
    ExactValue,
    GreaterThan,
    LessThan,
    Between,
    UnknownInitial
};

enum class NextScanType
{
    Equal,
    NotEqual,
    Increased,
    Decreased,
    Changed,
    Unchanged
};