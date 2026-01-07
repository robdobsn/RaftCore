#pragma once

typedef std::function<void(uint32_t segmentIdx, bool postShow, std::vector<LEDPixel>& ledPixels)> LEDPixelsShowCB;