#pragma once
#include <array>
#include <vector>

namespace dc {

// Lesebereich und Geschwindigkeit der Stücke für einen Aufruf von GrainStretch::process.
struct GrainFrame
{
    double step = 1.0;  // Quellsamples pro Ausgabesample (inklusive Tune und Pitch)
    double dir = 1.0;   // +1 vorwärts, -1 rückwärts
    bool cycle = false; // true: Lesepositionen in [lo, hi) umbrechen; false: auf [lo, hi - 1] halten
    double lo = 0.0;
    double hi = 0.0;
};

// Zyklischer Stretch im Stil des Akai S950: Alle grainLen Ausgabesamples startet an der Quellposition
// ein neues Stück, das alte läuft ein Viertel der Stückdauer weiter und wird linear übergeblendet.
// Die Tonhöhe kommt allein aus GrainFrame::step; die Quellposition liefert der Aufrufer.
// Mit jedem neuen Stück laufen alle älteren innerhalb der neuen Überblendzeit aus; es leben daher
// höchstens zwei Stücke, und die Gewichte bleiben auch bei wechselnder Stückgröße stetig.
class GrainStretch
{
public:
    void reset();
    // Beginnt das erste Stück sofort bei srcPos, ohne Einblendung (der Aufrufer überblendet selbst).
    void start(double srcPos, double dir, int grainLen);
    // Zieht das nächste Stück vor (Sprung der Quellposition): Es startet, sobald kein Stück mehr
    // ausblendet, und blendet dann über die normale Überblendzeit über. Hält „höchstens zwei“ ein.
    void retrigger();
    float process(const std::vector<float>& x, double srcPos, int grainLen, const GrainFrame& f);
    // Anzahl der gerade hörbaren Stücke (für Tests der Invariante „höchstens zwei“).
    int activeGrains() const;

private:
    struct Grain
    {
        bool active = false;
        bool fadingOut = false;
        double pos = 0.0;
        double dir = 1.0;
        float gain = 1.0f;
        float delta = 0.0f; // Gewichtsänderung pro Ausgabesample
        int remaining = 0;  // Samples bis zum Beginn der Ausblendung
        int fade = 1;       // Länge der Ausblendung in Samples
    };

    void launch(double srcPos, double dir, int grainLen, bool fadeIn);

    std::array<Grain, 2> grains_ {};
    int untilNext_ = 0;
};

} // namespace dc
