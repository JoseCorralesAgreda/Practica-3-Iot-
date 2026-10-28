#pragma once

/** Control the three active-high LEDs independently of networking. */
class LedController {
public:
    /** Create a controller without changing any hardware state. */
    LedController() = default;

    /** Configure the three outputs and switch all LEDs off. */
    void begin();

    /** Set red, yellow, or green; reject unknown or null colors unchanged. */
    bool setLed(const char* color, bool enabled);
};
