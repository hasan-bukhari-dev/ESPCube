#include "Buttons.h"
void Buttons::begin() {
    pinMode(Board::ButtonA, INPUT_PULLUP); pinMode(Board::ButtonB, INPUT_PULLUP); pinMode(Board::ButtonC, INPUT_PULLUP);
    const ButtonSnapshot s = sample(); lastLeft = s.left; lastMiddle = s.middle; lastRight = s.right;
}
ButtonSnapshot Buttons::sample() const {
    ButtonSnapshot snapshot;
    snapshot.left = digitalRead(Board::ButtonA);
    snapshot.middle = digitalRead(Board::ButtonB);
    snapshot.right = digitalRead(Board::ButtonC);
    return snapshot;
}
