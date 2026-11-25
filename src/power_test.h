#pragma once
// Power test (hidden menu, or serial key 'p'): steps through six power states, 15 s each, so the
// draw can be read off a USB meter. Each stage starts with that many beeps. Blocks until done.
void powerTestRun();
